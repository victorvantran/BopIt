/*
 * project5.c
 *
 * Created: 6/4/2019 11:33:42 AM
 * Author : Victor V. Tran
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "avr.h"
#include "lcd.h"



//// AVR Configuration
#define XTAL_FRQ 8000000lu



//// Bit manipulation
#define SET_BIT(p,i) ((p) |=  (1 << (i)))
#define CLR_BIT(p,i) ((p) &= ~(1 << (i)))
#define GET_BIT(p,i) ((p) &   (1 << (i)))

#define randomNumber(min, max) \
((rand() % (int)(((max) + 1) - (min))) + (min))



//// NOTE PITCH VALUES (OCTAVE_NOTE_SHARP)
#define REST -1
#define ONE_A 227
#define ONE_As 215
#define ONE_B 202
#define ONE_C 191
#define ONE_Cs 180
#define ONE_D 170
#define ONE_Ds 161
#define ONE_E 152
#define ONE_F 143
#define ONE_Fs 135
#define ONE_G 128
#define ONE_Gs 120
#define TWO_A 114
#define TWO_As 107
#define TWO_B 101
#define TWO_C 96
#define TWO_Cs 90
#define TWO_D 85
#define TWO_Ds 80
#define TWO_E 76
#define TWO_F 72
#define TWO_Fs 68
#define TWO_G 64
#define TWO_Gs 60
#define THREE_A 57
#define THREE_As 54
#define THREE_B 51
#define THREE_C 48
#define THREE_Cs 45
#define THREE_D 43
#define THREE_Ds 40
#define THREE_E 38
#define THREE_F 36
#define THREE_Fs 34
#define THREE_G 32
#define THREE_Gs 30



//// ISR Variables
#define SHUTDOWN_LIMIT 2000 // 2000 counts at 500ms = 1000 seconds before automatic shutoff.
int shutdownCounter = 0;
int shutdown = 0;
unsigned int channelIndex = 0;



//// Game Definitions
#define MENU_GAME 0
#define MENU_MAIN 1

#define MAX_LIVES 10

#define ROUND_FAIL 0
#define ROUND_SUCCESS 1

#define SAMPLE_TIME 50			// Time per sampling inputs
#define MAX_SAMPLE 1023 		// Analog value limit
#define MAX_TIMELIMIT 10000 	// Number of time steps (steps change may vary when difficulty of the game rises)
#define START_PERIOD 5000		// Period that determines how often steps change

#define INSUFFICIENT_INPUTS 0;
#define SUFFICIENT_INPUTS 1;
#define EXTRA_INPUTS 2;

#define PULL_PIN 0
#define TWIST_PIN 1

#define NUM_ADC_CHANNELS 2
int analogChannel[NUM_ADC_CHANNELS];	//Create a N position array to store the result of the ADC conversions




struct State {
	union {
		struct {
			unsigned char bop;
			unsigned char shake;
			unsigned char twist;
			unsigned char pull;
		};
		unsigned int elements[4];
	};
};



struct BopIt {
	unsigned char bopPulse;
	unsigned char shakePulse;
	unsigned char twistPulse;
	unsigned char pullPulse;
	
	unsigned int difficulty;
	unsigned int timeLimit;
	unsigned int score;
	unsigned int lives;
	unsigned int level;
	unsigned int highScore;
	
	unsigned char menuMode;
};



void initiateBopIt(struct BopIt *bopIt) {
	bopIt->bopPulse = 0;
	bopIt->shakePulse = 0;
	bopIt->twistPulse = 0;
	bopIt->pullPulse = 0;
		
	bopIt->difficulty = 0;
	bopIt->timeLimit = MAX_TIMELIMIT;
	bopIt->score = 0;
	bopIt->lives = MAX_LIVES;
	bopIt->level = 0;
	bopIt->highScore = 0;
	
	bopIt->menuMode = 1;
};



struct note {
	int frequency;
	int duration;
};



void note_wait(unsigned int usec){
	// Use Timer0 set up to run with prescalar of 8. Need this granularity for accurate notes.
	// Wait usec.
	TCCR0 = 2;

	while(usec--){
		TCNT0 = (unsigned char) (256 - (XTAL_FRQ / 8) * 0.000005);
		SET_BIT(TIFR,TOV0);
		WDR();
		while(!GET_BIT(TIFR,TOV0));
	}

	TCCR0 = 0;
	
	return;
}



void play_note(unsigned int frequency) {
	// Play a note based on frequency given. Frequency determines the pitch.
	int i;
	for (i = 0; i < 50; ++i) {
		SET_BIT(PORTA, 7);
		note_wait(frequency);
		CLR_BIT(PORTA, 7);
		note_wait(frequency);
	}
	avr_wait(10);
	for (i = 0; i < 50; ++i) {
		SET_BIT(PORTA, 7);
		note_wait(frequency);
		CLR_BIT(PORTA, 7);
		note_wait(frequency);
	}
	
	return;
}



struct configuration {
	int on;
	long total_samples;
	long num_samples;
	int instant_sample;
	int min_sample;
	int max_sample;
	int reset;
};



struct InputConfiguration {
	int on;
	long total_samples;
	long num_samples;
	int instant_sample;
	int min_sample;
	int max_sample;
	int reset;
};



void init_configuration(struct configuration* configuration) {
	configuration->on = 0;
	configuration->total_samples = 0;
	configuration->num_samples = 0;
	configuration->instant_sample = 0;
	configuration->min_sample = 1023;
	configuration->max_sample = 0;
	configuration->reset = 0;
}



struct Keypad {
	unsigned int key;
	unsigned char resetPress;
	unsigned char startPress;

	unsigned char choice;
	unsigned char incrementChoice;
	unsigned char decrementChoice;
		
	unsigned char incrementDifficulty;
	unsigned char decrementDifficulty;
};



void initiateKeypad(struct Keypad* keypad) {
	// Initialize keypad balues to 0.
	keypad->key = 0;
	keypad->resetPress = 0;
	keypad->startPress = 0;
	
	keypad->choice = 0;
	keypad->incrementChoice = 0;
	keypad->decrementChoice = 0;
		
	keypad->incrementDifficulty = 0;
	keypad->decrementDifficulty = 0;
}



int isPressed(int r, int c) {
	// Returns if a button on the keypad is pressed or not.
	DDRC = 0b00000000;
	
	SET_BIT(DDRC, c+4);
	SET_BIT(PORTC, r);
	
	return !GET_BIT(PINC, r);
}



unsigned int getKey(void) {
	// Return the value of key pressed.
	int r, c;
	for (r = 0; r < 4; ++r) {
		for (c = 0; c < 4; ++c) {
			if (isPressed(r,c)) {
				return ((r*4 + c) + 1);
			}
		}
	}
	return 0;
}



void initiateState(struct State *state) {
	// Resets the state of goal and/or reality.
	unsigned int numElements = (sizeof(state->elements)/sizeof(state->elements[0]));
	for (int i = 0; i < numElements; ++i) {
		state->elements[i] = 0;
	}
	return;
};



void randomizeGoal(struct State *goal) {
	// Sets a random set of action goals for the player to perform.
	unsigned int numElements = (sizeof(goal->elements)/sizeof(goal->elements[0]));
	int random = randomNumber(0, numElements);

	if (random == 0) {
		goal->bop = 1;
	} else if (random == 1) {
		goal->shake = 1;
	} else if (random == 2) {
		goal->twist = 1;
	} else if (random == 3) {
		goal->pull = 1;
	} else {	
		goal->bop = 1;
	}
	
	return;
};



unsigned short equalState(struct State *reality, struct State *goal) {
	// Determines if the input state of the player matches the goal state
	unsigned int numElements = (sizeof(goal->elements)/sizeof(goal->elements[0]));
	unsigned char correct = 1;
	
	for (int i = 0; i < numElements; ++i) {
		if (reality->elements[i] != goal->elements[i]) {
			if (reality->elements[i] > goal->elements[i]) {
				return EXTRA_INPUTS;
			} else { 
				return INSUFFICIENT_INPUTS;
			}
		}
	}
	
	return SUFFICIENT_INPUTS;
}



void readBopIt(struct BopIt *bopIt, struct State *reality) {
	// Update bop value.
	if (PINB & (1<<PB3)) {
		reality->bop = 1;
	}
}



void readShakeIt(struct BopIt *bopIt, struct State *reality) {
	// Update shake value.
	if (PINB & (1<<PB4)) {
		reality->shake = 0;
	} else {
		reality->shake = 1;
	}
}



void readPullIt(struct BopIt *bopIt, struct State *reality) {
	// Update pull value.
	unsigned short pullSample = analogChannel[0];
	if (pullSample > MAX_SAMPLE * 0.25) {
		reality->pull = 1;
	}
}



unsigned char getTwist(void) {
	// Return 1 if the potentiometer is twisted a certain amount and 0 if not.
	unsigned short twistSample = analogChannel[1];
	if (twistSample > MAX_SAMPLE * 0.9) {
		return 1;
	}
	return 0;
}
	


void readTwistIt(struct BopIt *bopIt, struct State *reality, unsigned char initialTwist) {
	// Update twist value
	unsigned char twist = getTwist();
	if (twist != initialTwist) {
		reality->twist = 1;
	}
}



unsigned char readReset(struct BopIt * bopIt) {
	// Reset the game if key 8 is pressed
	unsigned int key = getKey();
	if (key == 8) {
		bopIt->lives = 0;
		return 1;
	}
	return 0;
}



void displayCurrentAction(struct State *goal) {
	// Displays the action goal needed to be performed by the player
	lcd_pos(0,0);
	char top_buffer [16];
	
	if (goal->bop == 1) {
		sprintf(top_buffer, "    BOP-IT    ");
	} else if (goal->twist == 1) {
		sprintf(top_buffer, "    TWIST-IT   ");
	} else if (goal->pull == 1) {
		sprintf(top_buffer, "    PULL-IT    ");	
	} else if (goal->shake == 1) {
		sprintf(top_buffer, "  SHAKE-IT  ");
	} else {
		sprintf(top_buffer, "%d%d%d%d",goal->bop,goal->twist,goal->pull,goal->shake == 1);
	}

	lcd_puts2(top_buffer);
	return;
}



void colorCurrentAction(struct State *goal) {
	// Light a particular led to indicate actions/inputs needed to be performed by the player.
	if (goal->bop == 1) {
		SET_BIT(PORTA, 4);
	} else if (goal->twist == 1) {
		SET_BIT(PORTA, 3);
	} else if (goal->pull == 1) {
		SET_BIT(PORTA, 5);
	} else if (goal->shake == 1) {
		SET_BIT(PORTA, 3);	
		SET_BIT(PORTA, 4);
		SET_BIT(PORTA, 5);
	}
	avr_wait(100);
	CLR_BIT(PORTA, 3);
	CLR_BIT(PORTA, 4);
	CLR_BIT(PORTA, 5);
}



void soundCurrentAction(struct State *goal) {
	// Play a particular sound to indicate actions/inputs needed to be performed by the player.
	if (goal->bop == 1) {
		play_note(TWO_A);
	} else if (goal->twist == 1) {
		play_note(TWO_C);
	} else if (goal->pull == 1) {
		play_note(TWO_G);
	} else if (goal->shake == 1) {
		play_note(TWO_F);
	}
	
	return;
}


void displayCurrentScore(struct BopIt *bopIt) {
	// Display the current score and lives left.
	lcd_clr();
	lcd_pos(1,0);
	char bottom_buffer [16];
	sprintf(bottom_buffer, "Score:%d L:%d", bopIt->score, bopIt->lives);
	lcd_puts2(bottom_buffer);
	
	return;
}



void displayReality(struct State *reality) {
	// Display the current user input values
	lcd_pos(0,0);
	char top_buffer [16];
	sprintf(top_buffer, "p:%d t:%d", reality->pull, reality->twist);
	lcd_puts2(top_buffer);
	
	return;
}



void displayRoundFail(struct BopIt *bopIt) {
	// Display a negative message for a successful round
	displayCurrentScore(bopIt);
	lcd_pos(0,0);
	char top_buffer [16];
	sprintf(top_buffer, "      ----     ");
	lcd_puts2(top_buffer);
	
	return;
}



void displayRoundSuccess(struct BopIt *bopIt) {
	// Display a positive message for a successful round
	displayCurrentScore(bopIt);
	lcd_pos(0,0);
	char top_buffer [16];
	sprintf(top_buffer, "      ++++     ");
	lcd_puts2(top_buffer);
	
	return;
}


unsigned char round(struct BopIt *bopIt, struct State *reality, struct State *goal, int timeLimit) {
	// Creates a random goal for the player to perform. Executes sensory information for the goal through 
	// LCD display, color LEDS, and sound. Each round has a variable time limit. Player has that period
	// to perform all tasks (have reality state == goal state). If there are lack of actions after the 
	// period expires or extraneous actions, the round fails. Otherwise it is a success. Return the round result.
	
	initiateState(goal);
	initiateState(reality);
	randomizeGoal(goal);
	
	displayCurrentAction(goal);
	colorCurrentAction(goal);
	soundCurrentAction(goal);
	unsigned char initialTwist = getTwist();
	
	for (int i = 0; i < timeLimit; i = i + SAMPLE_TIME) {
		avr_wait(SAMPLE_TIME);
		readTwistIt(bopIt, reality, initialTwist);
		readPullIt(bopIt, reality);
		readBopIt(bopIt, reality);
		readShakeIt(bopIt, reality);
		if (readReset(bopIt) == 1) {
			return ROUND_FAIL;
		}
		unsigned short end = equalState(reality, goal);
		if (end == SUFFICIENT_INPUTS) { 
			bopIt->score = bopIt->score + 1;
			bopIt->level = bopIt->level + 1;
			unsigned int reamainingWait = (timeLimit - i) > 1000 ? 1000 : (timeLimit - i);
			displayRoundSuccess(bopIt);
			avr_wait(reamainingWait);
			return ROUND_SUCCESS;
		} else if (end == EXTRA_INPUTS) {
			bopIt->lives = bopIt->lives - 1;
			displayRoundFail(bopIt);
			unsigned int reamainingWait = (timeLimit - i) > 1000 ? 1000 : (timeLimit - i);
			avr_wait(reamainingWait);
			return ROUND_FAIL;
		}
	}
	
	// Not the correct inputs after given time limit
	if (equalState(reality, goal) == INSUFFICIENT_INPUTS || equalState(reality, goal) == EXTRA_INPUTS) {
		bopIt->lives = bopIt->lives - 1;
		displayRoundFail(bopIt);
		return ROUND_FAIL;
	}
	else
	{
		return ROUND_SUCCESS;
	}
}



void displayGameOver(unsigned int finalScore)
{
	// Display game over message and final score
	lcd_clr();
	lcd_pos(0,0);
	char top_buffer [16];
	sprintf(top_buffer, "    GAMEOVER    ");
	lcd_puts2(top_buffer);

	lcd_pos(1,0);
	char bottom_buffer [16];
	sprintf(bottom_buffer, "FinalScore:%d", finalScore);
	lcd_puts2(bottom_buffer);
	
	return;
}


void gameOver(struct BopIt *bopIt, struct State *reality, struct State *goal) {
	// Resets the goal and reality state for next game.
	// Resets the score, lives, and level for next game.
	// Display game over message for 5 seconds and return to main menu.
	initiateState(goal);
	initiateState(reality);
	unsigned int finalScore = bopIt->score;
	if (finalScore > bopIt->highScore) {
		bopIt->highScore = finalScore;
	}
	bopIt->score = 0;
	bopIt->lives = MAX_LIVES;
	bopIt->level = 0;
	
	displayGameOver(finalScore);
	
	avr_wait(5000);

	bopIt->menuMode = MENU_MAIN;
	
	return;
}



void displayMainMenu(struct BopIt* bopIt)
{
	// Displays the BOP-IT logo and high score
	lcd_pos(0,0);
	char top_buffer [16];
	sprintf(top_buffer, "     BOP-IT     ");
	lcd_puts2(top_buffer);

	lcd_pos(1,0);
	char bottom_buffer [16];
	sprintf(bottom_buffer, "Start HS:%d", (bopIt)->highScore);
	lcd_puts2(bottom_buffer);	
	
	return;
}



void runMainMenu(struct Keypad* keypad, struct BopIt* bopIt, struct State* goal, struct State* reality)
{
	// On main menu mode, display the bot it menu. 
	// Keep waiting until player presses the keys 4 and 0 to start to a game.
	while ((bopIt)->menuMode == MENU_MAIN) {
		displayMainMenu( bopIt );
				
		unsigned int key = getKey();
		if (key == 4) {
			(keypad)->startPress = 1;
		}
		if (key == 0) {
			if ((keypad)->startPress == 1) {
				(keypad)->startPress = 0;
				(bopIt)->menuMode = MENU_GAME;
				lcd_clr();
			}
		}
		avr_wait(100);
	}
	
	return;
}


void displayReadyForGame(struct BopIt* bopIt)
{
	// Give the player a 3 second notice that the game will begin.
	lcd_pos(0,0);
	char top_buffer [16];
	sprintf(top_buffer, "     BEGIN!     ");
	lcd_puts2(top_buffer);
				
	lcd_pos(1,0);
	char bottom_buffer [16];
	sprintf(bottom_buffer, "Score:%d L:%d", bopIt.score, bopIt.lives);
	lcd_puts2(bottom_buffer);
	avr_wait(3000);
	
	return;
}


void runGame(struct Keypad* keypad, struct BopIt* bopIt, struct State* goal, struct State* reality)
{
	// Run rounds until game over. After ever successful round, the period gets smaller.
	// This means the margin of time for the player to perform actions is smaller.
	unsigned int period = START_PERIOD;		
	while ((bopIt)->menuMode == MENU_GAME) {		
		shutdown = 0;
		unsigned char success = 0;
		success = round(bopIt, reality, goal, period);
		period = period - 100;
		if (period < 300) {
			period = 300;
		}
		if (success == 0 && (bopIt)->lives <= 0) {
			gameOver(bopIt, reality, goal);
			break;
		}
		if ((bopIt)->lives <= 0) {
			gameOver(bopIt, reality, goal);
			break;
		}
	}
	
	return;
}



ISR(TIMER1_OVF_vect) {
	// Shutdown counter that increments every 500ms.
	shutdownCounter = shutdownCounter + 1;
	TCNT1 = 49910;
}



ISR(ADC_vect) {
	// Samples the analog-to-digital value and 
	// sets the proper analogChannel to said value.
	// Prepare samples for the next analogChannel.
	analogChannel[channelIndex] = ADC;				// Set proper analog channel to proper digital value
	channelIndex = channelIndex + 1;				// Increment and wrap around to update other analog channels
	if (i > NUM_ADC_CHANNELS) {i=0;}
	
	ADMUX = (1<<REFS0) | (1<<REFS0) | channelIndex;	// Select ADC Channel based on channelIndex
	ADCSRA |= (1 << ADSC);   						// Start A2D Conversions for the next ADC
}



int main(void) {
	srand(time(NULL));
	DDRA = 0b10111111;			// Data Direction Register: Set input/output pins of portA
	lcd_init();					// Initiate LCD Screen proper ports setup
	
	TCCR1B |= (1<<CS12); 		// Timer1 set 256 as Prescalar for higher period
	TCNT1 = 49910; 				// Let Timer1 count start at 49910 in preparation for overflow interrupt at 65535. This gives 15625 steps at granularity of (256/8000000) = 0.5s
	TIMSK |= (1<<TOIE1); 		// Timer interrupt Mask Register (TISMK). TOIE1 bit set, so global interrupts are enabled. Therefore ISR can be called
	
	ADMUX = (1<<REFS0);			// Set Analog-to-Digital-Converter (ADC) voltage reference to AVCC
	ADCSRA |= (1 << ADEN);		// Enable ADC
	ADCSRA |= (1 << ADIE);		// Enable ADC Interrupt
	ADCSRA |= (1 << ADPS2);		// Set ADC sample rate (13us)
	ADCSRA |= (1 << ADSC);		// Start A2D Conversions
	sei();						// Enables interrupts by setting global interrupt mask
	
	struct Keypad keypad;
	struct BopIt bopIt;
	struct State goal;
	struct State reality;
	
	initiateKeypad(&keypad);
	initiateBopIt(&bopIt);
	initiateState(&goal);
	initiateState(&reality);
	
	while (1) {		
		lcd_clr();
		runMainMenu(&keypad, &bopIt, &goal, &reality);
		displayReadyForGame(&bopIt);
		runGame(&keypad, &bopIt, &goal, &reality);
	}
	
}



