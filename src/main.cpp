#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <util/atomic.h> 

#include "usitwislave.h"

// pin definitions
#define MASTER_IRQ    PA1

#define COL_A         PB3
#define COL_B         PB4
#define COL_C         PB5
#define COL_D         PB6

#define ROW_1         PA3
#define ROW_2         PA4
#define ROW_3         PA5
#define ROW_4         PA6
#define ROW_5         PA7

// how long to wait between switching columns on / off
// !!!====> Use EVEN numbers only <====!!!
#define COLUMN_DELAY_MS  10 // 10 milliseconds

// row button definitions. For example, row_1 has four buttons in (l-t-r) positions 7, 9, 8 and 6.
// these lists are zero indexed. Values of 254 are null (i.e., these buttons don't exist in the mapping). 
const uint8_t row_1_buttons[4] = { 8, 6, 7, 9 };
const uint8_t row_2_buttons[4] = { 254, 254, 10, 254 };
const uint8_t row_3_buttons[4] = { 12, 15, 16, 11 };
const uint8_t row_4_buttons[4] = { 1, 2, 0, 5 };
const uint8_t row_5_buttons[4] = { 14, 4, 3, 13 };

// stores the transient state (for the state machine)
uint8_t transient[17] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
// stores the button press states, essentially this is our return values
uint8_t pressed[17]   = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// stores the next expected state for the next Col (relates to below). Toggles between zero and one.
volatile uint8_t nextState = 0;
// stores the current column that's high
volatile uint8_t currentCol = COL_A;
// are we currently checking pins?
volatile uint8_t nowScanning = 0;
volatile bool scanPins = false;

// this function checks to see if a given row is high or low, translating to a button press
void checkPin(uint8_t pin, uint8_t button) {
  if (button < 254) {
    // check state of row
    uint8_t value = (PINA & _BV(pin)) ? 1 : 0;

    if (transient[button] != value) {
      transient[button] = value;
      if (value == 1) {
        pressed[button] = 1;
        PORTA |= (1 << MASTER_IRQ);
      }
    }
  }
}

// isr called when PCINTx is triggered
ISR(PCINT_vect) {
  if (nowScanning) {
    uint8_t btnIndex = currentCol - 3;

    // check the pins to see if they're high (a button was pressed)
    checkPin(ROW_1, row_1_buttons[btnIndex]);
    checkPin(ROW_2, row_2_buttons[btnIndex]);
    checkPin(ROW_3, row_3_buttons[btnIndex]);
    checkPin(ROW_4, row_4_buttons[btnIndex]);
    checkPin(ROW_5, row_5_buttons[btnIndex]);
  }
}

// counter to show how many cycles have passed (how many times timer has overflowed)
volatile uint8_t intr_wait = 0;
// isr that's fired when timer1 overflows
ISR (TIMER1_OVF_vect) {
  // if we still need to wait
  if (intr_wait <= (COLUMN_DELAY_MS / 2)) {
    intr_wait++;
  } else {
    // reset the wait cucles
    intr_wait = 0;

    // do what needs to be done (it's been ~4ms)
    if (nextState == 0) {
      // if we're turning off a pin, turn it off
      PORTB &= ~(1 << currentCol);

      // next state will be to turn it off
      nextState = !nextState;

      nowScanning = 1;
    } else {
      nowScanning = 0;

      // if we need to turn on a pin, turn it on
      PORTB |= (1 << currentCol);

      // next state will be to turn on a pin
      nextState = !nextState;
      // and that will be the next pin
      currentCol++;
      // if the next pin is out of bounds, go back to first
      if (currentCol > COL_D) {
        currentCol = COL_A;
      }
    }
  }
}

static void request(volatile uint8_t input_buffer_length, const uint8_t *input_buffer, uint8_t *output_buffer_length, uint8_t *output_buffer) {
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    *output_buffer_length = 17;
    for (uint8_t i = 0; i < 17; i++) {
      output_buffer[i] = pressed[i];
      pressed[i] = 0;
    }
  }
  
  // clear the interrupt to the master
  PORTA &= ~(1 << MASTER_IRQ);
}

void columnSetup() {
  // set the columns as outputs
  DDRB |= (1 << COL_A);
  DDRB |= (1 << COL_B);
  DDRB |= (1 << COL_C);
  DDRB |= (1 << COL_D);

  PORTB |= (1 << COL_A);
  PORTB |= (1 << COL_B);
  PORTB |= (1 << COL_C);
  PORTB |= (1 << COL_D);

  // set timer for columns
  TCCR1A = 0x00; // normal mode for timer1
  TCCR1B = 0x00; // normal mode for timer1
  TCCR1B |= (1 << CS00) | (1 << CS01); // 64 prescaling (2ms) for timer1
  TCNT1 = 0; // set the timer1 counter to zero
  TIMSK |= (1 << TOIE1); // enable timer1
}

void rowSetup() {
  DDRA &= ~(1 << ROW_1); // set row 1 to input
  DDRA &= ~(1 << ROW_2); // set row 2 to input
  DDRA &= ~(1 << ROW_3); // set row 2 to input
  DDRA &= ~(1 << ROW_4); // set row 2 to input
  DDRA &= ~(1 << ROW_5); // set row 2 to input

  PORTA |= (1 << ROW_1);
  PORTA |= (1 << ROW_2);
  PORTA |= (1 << ROW_3);
  PORTA |= (1 << ROW_4);
  PORTA |= (1 << ROW_5);

  GIMSK |= (1 << PCIE0); // enables pin change interrupts
  GIMSK |= (1 << PCIE1); // enables pin change interrupts

  PCMSK0 |= (1 << ROW_1); // enables interrupt for ROW_1
  PCMSK0 |= (1 << ROW_2); // enables interrupt for ROW_2
  PCMSK0 |= (1 << ROW_3); // enables interrupt for ROW_2
  PCMSK0 |= (1 << ROW_4); // enables interrupt for ROW_2
  PCMSK0 |= (1 << ROW_5); // enables interrupt for ROW_2
}

int main() {
  // sets irq to output
  DDRA |= (1 << MASTER_IRQ);

  // wait for the io pins to settle
  _delay_ms(50);

  //set up the rows
  rowSetup();
  // wait for pcints to set up
  _delay_ms(50);

  //set up the rows
  columnSetup();
  // wait for timer to set up
  _delay_ms(50);

  // enable the i2c slave
  usi_twi_slave(0x10, 0, request, nullptr);

  // enable interrupts globally
  sei();

  // we don't have anything in the loop
  while(1);
}
