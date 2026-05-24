#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

// Definire praguri distanta
#define PRAG_CRITIC    20
#define PRAG_AVERTIZARE 40

// Variabile globale pentru masurarea distantei
volatile uint8_t echo_stare = 0;
volatile uint32_t ovf_counter = 0;
volatile uint8_t masuratoare_noua = 0;
volatile uint32_t distanta_cm = 0;

// Initializare pini I/O
void GPIO_init(void) {
	DDRD |= (1 << DDD2) | (1 << DDD4) | (1 << DDD5) | (1 << DDD6) | (1 << DDD7);
	DDRD &= ~(1 << DDD3); // PD3 (ECHO) este intrare

	DDRB |= (1 << DDB1) | (1 << DDB2);
	DDRB &= ~(1 << DDB3);
	PORTB |= (1 << PORTB3); // Pull-up intern pe buton
}

// Initializare Transmisie Seriala UART
void UART_init(uint32_t baud) {
	uint16_t ubrr = F_CPU / 16 / baud - 1;
	UBRR0H = (uint8_t)(ubrr >> 8);
	UBRR0L = (uint8_t)ubrr;
	
	UCSR0B |= (1 << TXEN0);
	UCSR0C |= (1 << UCSZ01) | (1 << UCSZ00);
}

void UART_transmit(char data) {
	while (!(UCSR0A & (1 << UDRE0)));
	UDR0 = data;
}

void UART_print(const char* str) {
	while (*str) {
		UART_transmit(*str++);
	}
}

// Initializare Intrerupere Externa INT1
void Interrupt_init(void) {
	EICRA |= (1 << ISC10);
	EIMSK |= (1 << INT1);
}

// Initializare Timere
void Timers_init(void) {
	// Timer 1
	ICR1 = 39999;
	TCCR1A |= (1 << COM1A1) | (1 << COM1B1) | (1 << WGM11);
	TCCR1B |= (1 << WGM13) | (1 << WGM12) | (1 << CS11);

	OCR1A = 3000;
	OCR1B = 3000;

	// Timer 2
	TCCR2A = 0;
	TCCR2B = 0;
	TIMSK2 |= (1 << TOIE2);
}

ISR(INT1_vect) {
	if (PIND & (1 << PIND3)) {
		TCNT2 = 0;
		ovf_counter = 0;
		TCCR2B |= (1 << CS21);
		echo_stare = 1;
		} else {
		TCCR2B &= ~((1 << CS22) | (1 << CS21) | (1 << CS20));
		if (echo_stare == 1) {
			uint32_t ticks = ((uint32_t)ovf_counter * 256) + TCNT2;
			distanta_cm = (ticks / 2) / 58;
			masuratoare_noua = 1;
			echo_stare = 0;
		}
	}
}

ISR(TIMER2_OVF_vect) {
	if (echo_stare == 1) {
		ovf_counter++;
	}
}

void Trigger_Senzor(void) {
	PORTD &= ~(1 << PORTD2);
	_delay_us(2);
	PORTD |= (1 << PORTD2);
	_delay_us(10);
	PORTD &= ~(1 << PORTD2);
}

void Actualizare_Feedback_Vizual(uint32_t dist) {
	if (dist < PRAG_CRITIC) {
		PORTD |= (1 << PORTD5) | (1 << PORTD4); // Rosu + Laser
		PORTD &= ~((1 << PORTD6) | (1 << PORTD7));
		} else if (dist >= PRAG_CRITIC && dist <= PRAG_AVERTIZARE) {
		PORTD |= (1 << PORTD6); // Galben
		PORTD &= ~((1 << PORTD4) | (1 << PORTD5) | (1 << PORTD7));
		} else {
		PORTD |= (1 << PORTD7); // Verde
		PORTD &= ~((1 << PORTD4) | (1 << PORTD5) | (1 << PORTD6));
	}
}

int main(void) {
	GPIO_init();
	UART_init(9600);
	Timers_init();
	Interrupt_init();
	
	sei();
	
	UART_print("\r\nSistem Tureta Radar 2D Initializat.\r\n");

	uint8_t sistem_activ = 0;
	uint8_t buton_apasat = 0;
	
	uint16_t unghi_pan = 2000;
	int16_t directie_pan = 50;
	
	uint16_t unghi_tilt = 3000;
	int16_t directie_tilt = 100;
	
	char buffer_uart[128];
	uint8_t contor_mesaje = 0;

	while (1) {
		// Citire Buton START/STOP
		if (!(PINB & (1 << PINB3))) {
			if (!buton_apasat) {
				_delay_ms(50);
				if (!(PINB & (1 << PINB3))) {
					sistem_activ = !sistem_activ;
					buton_apasat = 1;
					if (!sistem_activ) {
						PORTD &= ~((1 << PORTD4) | (1 << PORTD5) | (1 << PORTD6) | (1 << PORTD7));
						OCR1A = 3000;
						OCR1B = 3000;
						UART_print("Sistem OPRIT.\r\n");
						} else {
						UART_print("Sistem ACTIVAT (Scanare Matriceala 2D).\r\n");
					}
				}
			}
			} else {
			buton_apasat = 0;
		}

		if (sistem_activ) {
			OCR1A = unghi_pan;
			OCR1B = unghi_tilt;
			
			Trigger_Senzor();
			_delay_ms(20);

			if (masuratoare_noua) {
				masuratoare_noua = 0;
				
				uint16_t grade_pan = (unghi_pan - 2000) * 180 / 2000;
				uint16_t grade_tilt = (unghi_tilt - 2000) * 180 / 2000;
				
				Actualizare_Feedback_Vizual(distanta_cm);
				
				// Logica de filtrare si deplasare 
				if (distanta_cm < PRAG_CRITIC) {
					// Target Lock
					sprintf(buffer_uart, ">>> TARGET LOCK! PAN: %d deg | TILT: %d deg | Dist: %ld cm\r\n", grade_pan, grade_tilt, distanta_cm);
					UART_print(buffer_uart);
					contor_mesaje = 0;
					_delay_ms(500);
					} else {
					// Scanare
					contor_mesaje++;
					if (contor_mesaje >= 20) {
						sprintf(buffer_uart, "PAN: %d deg | TILT: %d deg | Distanta: %ld cm\r\n", grade_pan, grade_tilt, distanta_cm);
						UART_print(buffer_uart);
						contor_mesaje = 0;
					}
					
					// Viteza de deplasare
					if (distanta_cm <= PRAG_AVERTIZARE) {
						_delay_ms(60);
						} else {
						_delay_ms(25);
					}
					
					unghi_pan += directie_pan;
					
					// Logica Raster (Baleiaj 2D)
					if (unghi_pan >= 4000 || unghi_pan <= 2000) {
						if (unghi_pan >= 4000) { unghi_pan = 4000; directie_pan = -50; }
						if (unghi_pan <= 2000) { unghi_pan = 2000; directie_pan = 50; }
						
						unghi_tilt += directie_tilt;
						
						if (unghi_tilt >= 3500) {
							unghi_tilt = 3500;
							directie_tilt = -100;
							} else if (unghi_tilt <= 2500) {
							unghi_tilt = 2500;
							directie_tilt = 100;
						}
					}
				}
			}
			} else {
			_delay_ms(100);
		}
	}
	return 0;
}