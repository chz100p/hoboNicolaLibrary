/**
 * 
  xd64.cpp   XD64 (rev3) PCB interfaces of "Hobo-nicola keyboard and adapter library".
  Copyright (c) 2023 Takeshi Higasa
  
  This file is part of "Hobo-nicola keyboard and adapter".

  "Hobo-nicola keyboard and adapter" is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  "Hobo-nicola keyboard and adapter" is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with "Hobo-nicola keyboard and adapter".  If not, see <http://www.gnu.org/licenses/>.
  
  Included in hoboNicola 1.7.5.		Feb. 22. 2024.*/

#include <avr/io.h>
#include <avr/wdt.h>
#include <Arduino.h>
#include "xd64.h"
#include "hid_keycode.h"

// 16000000 / 64 / 250 / 5 = 200(Hz) 
static const uint16_t TIMER1_INTERVAL = -250 * 5;  // 0xfb1d

// switch matrix info.
static const uint8_t ROW_COUNT = 5;
static const uint8_t COL_COUNT = 14;

static const uint8_t STATE_COUNT = ROW_COUNT * 2; // 16bits@row
static const uint8_t SW_COUNT = ROW_COUNT * COL_COUNT;

static uint8_t key_state[STATE_COUNT]; // current sw states.
static uint8_t last_key_state[STATE_COUNT];  // previous sw states.

// 
static void read_cols(uint8_t** pp) {
	uint8_t* p = *pp;
	*p++ = ~( ((PINB << 6) & 0x80) | ((PIND << 2) & 0x40) | ((PINB >> 1) & 0x20) | ((PINC >> 2) & 0x10) | 
		((PINC >> 4) & 8) | ((PINE >> 4) & 4) | (PINF & 3));
	*p = ~( ((PINB << 2) & 0x20) | ((PIND >> 2) & 0x10) | ((PIND >> 4) & 8) | 
		((PINB >> 2) & 4) | ((PINB >> 4) & 2) | ((PINB >> 7) & 1)) & 0x3f;
	*pp += 2;
}

#define READ_ROW(port, pin, pp)  { \
	port &= ~_BV(pin); \
	delayMicroseconds(5); \
	read_cols(pp); \
	port |= _BV(pin); \
	delayMicroseconds(5); }

/**
 * @brief The states of the switch is determined only when the results of two consecutive scans match. 
 * 			The code is generated by comparing them with the stored last determined switch states.
 */
void matrix_scan() {
	static uint8_t tmp_state[STATE_COUNT];
	cli();
	// read current sw stetes and store.
	uint8_t *p = tmp_state;
	READ_ROW(PORTD, 0, &p);
	READ_ROW(PORTD, 1, &p);
	READ_ROW(PORTD, 2, &p);
	READ_ROW(PORTD, 3, &p);
	READ_ROW(PORTD, 5, &p);
	sei();
	int n = memcmp(tmp_state, key_state, STATE_COUNT);
	if (n != 0) { // sw states not match. save current states for next time.
		memcpy(key_state, tmp_state, STATE_COUNT);
		return;
	}
	uint8_t code = 1;
	for(uint8_t i = 0; i < sizeof(tmp_state); i++) {
		uint8_t pressed = tmp_state[i];
		uint8_t delta = (i & 1) ? 6 : 8;
		uint8_t change = pressed ^ last_key_state[i];
		if (change) {
			uint8_t mask = 1;
			for(uint8_t k = 0; k < delta; k++, mask <<= 1) {
				if (change & mask) {
				uint8_t c = code + k;
				c |= (pressed & mask) ? 0 : 0x80; // release なら 0x80をOR.
				xd_put_buffer(c);
				}
			}
			last_key_state[i] = pressed;  
		}
		code += delta;
	}
}

// TC1 interval interrupt service routine.
ISR(TIMER1_OVF_vect, ISR_NOBLOCK) {
	matrix_scan();
	TCNT1 = TIMER1_INTERVAL;
}

// scancode --> HID UsageID
// usageid = hid_table[ scancode - 1 ];
static const uint8_t HID_TABLE_BASE = 0;
static const uint8_t HID_TABLE_FN1 = 1;

static const uint8_t scan_to_hid_table[2][SW_COUNT] = {
  {     
	HID_ESCAPE,   HID_1,         HID_2,        HID_3,        HID_4,       HID_5,      HID_6,       HID_7,     HID_8,       HID_9, // sw1～10
	HID_0,        HID_MINUS,     HID_EQL,      HID_J_BSLASH, HID_TAB,     HID_Q,      HID_W,       HID_E,     HID_R,       HID_T,// sw11～20
	HID_Y,        HID_U,         HID_I,        HID_O,        HID_P,       HID_J_AT,   HID_J_LBRACK,HID_UNDEF, HID_CAPS,    HID_A,  // sw21～30
	HID_S,        HID_D,         HID_F,        HID_G,        HID_H,       HID_J,      HID_K,       HID_L,     HID_SEMICOL, HID_QUOTE,   // sw31～40
	HID_J_RBR_32, HID_ENTER,     HID_L_SHIFT,  HID_UNDEF,    HID_Z,       HID_X,      HID_C,       HID_V,     HID_B,       HID_N,  // sw41～50
	HID_M,        HID_COMMA,     HID_PERIOD,   HID_SLASH,    HID_DELETE,  HID_U_ARROW,HID_L_CTRL,  HID_L_GUI, HID_L_ALT,   HID_UNDEF,   // sw51～60
	HID_HIRAGANA, HID_SPACE,     HID_MUHENKAN, HID_J_UL,     HID_L_ARROW, HID_BACKSP, HID_R_CTRL,  HID_X_FN1, HID_D_ARROW, HID_R_ARROW  // sw61～70
},
  {     
	HID_GRAVE_AC, HID_F1,        HID_F2,       HID_F3,       HID_F4,      HID_F5,     HID_F6,      HID_F7,    HID_F8,      HID_F9, // sw1～10
	HID_F10,      HID_F11,       HID_F12,      HID_INSERT,   HID_TAB,     HID_F13,    HID_F14,     HID_F15,   HID_F16,     HID_F17,// sw11～20
	HID_F18,      HID_F19,       HID_F20,      HID_F21,      HID_PRNTSCRN,HID_SCRLOCK,HID_PAUSE,   HID_UNDEF, HID_CAPS,    HID_A,// sw21～30
	HID_S,        HID_D,         HID_F,        HID_G,        HID_H,       HID_J,      HID_K,       HID_L,     HID_SEMICOL, HID_QUOTE,   // sw31～40
	HID_J_RBR_32, HID_ENTER,     HID_L_SHIFT,  HID_UNDEF,    HID_Z,       HID_X,      HID_C,       HID_V,     HID_B,       HID_N,  // sw41～50
	FN_MEDIA_MUTE,FN_MEDIA_V_DN, FN_MEDIA_V_UP,HID_SLASH,    HID_DELETE,  HID_PGUP,   HID_L_CTRL,  HID_L_GUI, HID_L_ALT,   HID_UNDEF,   // sw51～60
	HID_APP,      HID_HENKAN,    HID_SPACE,    HID_J_UL,     HID_HOME,    HID_BACKSP, HID_R_CTRL,  HID_X_FN1, HID_PGDOWN,  HID_END  // sw61～70
  }
};

static uint8_t hid_table_index = HID_TABLE_BASE;
void xd64_table_change(uint8_t key, bool pressed) {
	if (key == HID_X_FN1 && pressed)
		hid_table_index = HID_TABLE_FN1;
	else
		hid_table_index = HID_TABLE_BASE;
}

/**
 * WindowsをUSレイアウトとし、hoboNicolaでU: US Layout を選択しているとき
 * 以下のようなレイアウトに変更する。
 * 物理的なレイアウトはJIS。
*/
uint8_t xd64_get_key(bool& pressed, bool us_layout) { 
	uint8_t fn = hid_table_index == HID_TABLE_FN1;
	uint8_t k = xd_get_buffer();
	if (k == 0) return 0;
	pressed = ((k & 0x80) == 0);
	k &= 0x7f;
	if (k <= SW_COUNT) {
		uint8_t hid = scan_to_hid_table[hid_table_index][k - 1];
		if (hid != 0) {
			if (us_layout) {
				if (hid == HID_CAPS && !fn)
					return HID_IME_OFF;	// CapsLock = Fn + Caps
				switch(hid) {
				case HID_J_BSLASH:
					return HID_BSLASH;	// \ |
				case HID_J_AT:
					return HID_LBRACK;	// ` ~
				case HID_J_LBRACK:
					return HID_RBRACK;		// [
				//case HID_J_RBR_32:
					//return HID_RBRACK;		// ] NICOLA時の取消キーとして残す
				case HID_J_UL:
					return fn ? hid : HID_R_SHIFT;	// USのとき Fn + _ で _ を出す( MacOS )
				case HID_MUHENKAN:
					return HID_F14;
				case HID_HENKAN:
					return HID_F15;
				case HID_HIRAGANA:
					//return HID_F16;
					return HID_IME_ON;
				default:
					break;
				}
			}
			return hid;
		}
	}
	return 0;
}
// iom32u4.hにPRTIM4が定義されていない
#ifndef PRIM4
#define PRTIM4 4
#endif

void init_xd64() {
	cli();
// disable Watchdog reset. To prevent stall caused by touch-1200.
	MCUSR = 0;
	wdt_disable();  

	CLKPR = 0x80;
	CLKPR = 0;    // CLKPR = 1/1
	uint8_t v = MCUCR | 0x80;
	MCUCR = v;    // disable JTAG for PF
	MCUCR = v;

//3mA ほど省エネ
	ADCSRA = ADCSRA & 0x7f;
	ACSR = ACSR | 0x80;
	PRR0 = _BV(PRTWI) | _BV(PRSPI) | _BV(PRADC);		
	PRR1 = _BV(PRTIM4) | _BV(PRTIM3) | _BV(PRUSART1);	//
  
	TCCR1A = 0; // init timers (without TC0)
	TCCR3A = 0;
	TCCR4A = 0;

// setup Ports.
	DDRB =  _BV(2); // PB2 : Caps indicator.
	DDRC =  0;
	DDRD =  _BV(0) | _BV(1) | _BV(2) | _BV(3) | _BV(5);
	DDRE =  0;
	DDRF =  _BV(5) | _BV(6);  // BGLED, RGBLED
// set INPUT_PULLUP to COLs.
	PORTB = _BV(1) | _BV(3) | _BV(4) | _BV(5) | _BV(6) | _BV(7) ;
	PORTC = _BV(6) | _BV(7) ;
	PORTD = _BV(4) | _BV(6) | _BV(7);
	PORTE = _BV(6);
	PORTF = _BV(0) | _BV(1) ;
// set output HIGH to all ROWs. BGLED off and CapsLED off.
	PORTB = PORTB | _BV(2); // PB2 : Caps indicator.
	PORTD = PORTD | _BV(0) | _BV(1) | _BV(2) | _BV(3) | _BV(5);
	PORTF = PORTF | _BV(5); // BGLED off.
// setup timer1 interrupt (period = 5msec)
	TCCR1A = 0;
	TCCR1B = 3; // CLKio / 64　 = 250KHz.
	TCCR1C = 0;
	TCNT1 = TIMER1_INTERVAL;
	TIMSK1 = 1; // enable Timer1 Overflow Interrupt
	xd_clear_buffer();
	sei();
}
