#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "usitwislave.h"

// pin definitions
#define MASTER_IRQ    PA1

#define COL_A         PA4
#define COL_B         PA5
#define COL_C         PA6
#define COL_D         PA7

#define ROW_1         PB3
#define ROW_2         PB4
#define ROW_3         PB5
#define ROW_4         PB6
#define ROW_5         PA3

// how long to wait between switching columns on / off
// !!!====> Use EVEN numbers only <====!!!
#define COLUMN_DELAY_MS  10 // 10 milliseconds

// row button definitions. For example, row_1 has four buttons in (l-t-r) positions 7, 9, 8 and 6.
// these lists are zero indexed. Values of 254 are null (i.e., these buttons don't exist in the mapping).
uint8_t row_1_buttons[4] = { 7, 9, 8, 6 };
uint8_t row_2_buttons[4] = { 254, 254, 5, 254 };
uint8_t row_3_buttons[4] = { 12, 15, 16, 11 };
uint8_t row_4_buttons[4] = { 1, 0, 3, 10 };
uint8_t row_5_buttons[4] = { 14, 2, 4, 13 };

// stores the button state (was it pressed?)
uint8_t pressed[17] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
// stores the transient state (for the state machine)
uint8_t transient[17] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// stores the next expected state for the next Col (relates to below). Toggles between zero and one.
uint8_t nextState = 1;
// stores the current column that's high
uint8_t currentCol = COL_A;

// this function checks to see if a given row is high or low, translating to a button press
void checkPin(volatile uint8_t *reg, uint8_t pin, uint8_t button) {
  // check state of row
  uint8_t value = (*reg & _BV(pin)) ? 1 : 0;

  // if the row is high
  if (value == 1) {
    // show that the button was pre7, 254, 12, 1, 14ssed down
    transient[button] = 1;
  // otherwise, the pin is low. Check to see if it was high previously
  } else if (transient[button] == 1) {
    // show that the button was released
    transient[button] = 0;
    // show that the button was pressed
    pressed[button] = 1;
    // send an interrupt to the master
    PORTA |= (1 << MASTER_IRQ);
  }
}

// isr called when the timer overflows
ISR(PCINT_vect) {
  // default is COL_A (index 0)
  uint8_t btnIndex = 0;
  // check to see if a different column is active
  if (currentCol == COL_B) {
    btnIndex = 1;
  } else if (currentCol == COL_C) {
    btnIndex = 2;
  } else if (currentCol == COL_D) {
    btnIndex = 3;
  }

  // check the pins to see if they're high (a button was pressed)
  checkPin(&PINB, ROW_1, row_1_buttons[btnIndex]);
  checkPin(&PINB, ROW_2, row_2_buttons[btnIndex]);
  checkPin(&PINB, ROW_3, row_3_buttons[btnIndex]);
  checkPin(&PINB, ROW_4, row_4_buttons[btnIndex]);
  checkPin(&PINA, ROW_5, row_5_buttons[btnIndex]);
}

// counter to show how many cycles have passed (how many times timer has overflowed)
uint8_t intr_wait = 0;
// isr that's fired when timer1 overflows
ISR (TIMER1_OVF_vect) {
  // if we still need to wait
  if (intr_wait <= (COLUMN_DELAY_MS / 2)) {
    intr_wait++;
  } else {
    // reset the wait cucles
    intr_wait = 0;

    // do what needs to be done (it's been ~4ms)
    if (nextState == 1) {
      // if we need to turn on a pin, turn it on
      PORTA |= (1 << currentCol);
      // next state will be to turn it off
      nextState = !nextState;
    } else {
      // if we're turning off a pin, turn it off
      PORTA &= ~(1 << currentCol);
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

void columnSetup() {
  // set the columns as outputs
  DDRA |= (1 << COL_A);
  DDRA |= (1 << COL_B);
  DDRA |= (1 << COL_C);
  DDRA |= (1 << COL_D);

  // set timer for columns
  TCCR1A = 0x00; // normal mode for timer1
  TCCR1B = 0x00; // normal mode for timer1
  TCCR1B |= (1 << CS00) | (1 << CS01); // 64 prescaling (2ms) for timer1
  TCNT1 = 0; // set the timer1 counter to zero
  TIMSK |= (1 << TOIE1); // enable timer1
}

void rowSetup() {
  DDRB &= ~(1 << ROW_1); // set row 1 to input
  DDRB &= ~(1 << ROW_2); // set row 2 to input
  GIMSK |= (1 << PCIE0); // enables pin change interrupts
  GIMSK |= (1 << PCIE1); // enables pin change interrupts
  PCMSK0 |= (1 << ROW_1); // enables interrupt for ROW_1
  PCMSK1 |= (1 << ROW_2); // enables interrupt for ROW_2
}

uint32_t getVal() {
  uint32_t val = 0b0;
  for (uint8_t i; i < 17; i++) {
    if (pressed[i] == 1) {
      val |= (1 << i);
    }
    pressed[i] = 0;
  }
  return val;
}

static void request(volatile uint8_t input_buffer_length, const uint8_t *input_buffer, uint8_t *output_buffer_length, uint8_t *output_buffer) {
  uint32_t val = getVal();

  *output_buffer_length = 4;

  output_buffer[0] = (val >> 24) & 0xFF;
  output_buffer[1] = (val >> 16) & 0xFF;
  output_buffer[2] = (val >> 8) & 0xFF;
  output_buffer[3] = val & 0xFF;

  // clear the interrupt to the master
  PORTA &= ~(1 << MASTER_IRQ);
}

int main() {
  // sets irq to output
  DDRA |= (1<<MASTER_IRQ)|(1<<MASTER_IRQ);

  // wait for the io pins to settle
  _delay_ms(500);

  //set up the rows
  rowSetup();
  // wait for pcints to set up
  _delay_ms(300);

  //set up the rows
  columnSetup();
  // wait for timer to set up
  _delay_ms(300);

  // enable interrupts globally
  sei();

  usi_twi_slave(0x10, 0, request, nullptr);

  // we don't have anything in the loop
  while(1);
}
