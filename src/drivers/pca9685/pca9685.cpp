/****************************************************************************
 *
 *   Copyright (c) 2012-2014 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file pca9685.cpp
 *
 * Driver for the PCA9685 I2C PWM module
 * The chip is used on the Adafruit I2C/PWM converter https://www.adafruit.com/product/815
 *
 * Parts of the code are adapted from the arduino library for the board
 * https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library
 * for the license of these parts see the
 * arduino_Adafruit_PWM_Servo_Driver_Library_license.txt file
 * see https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library for contributors
 *
 * @author Thomas Gubler <thomasgubler@gmail.com>
 */

#include <px4_config.h>
#include <px4_defines.h>

#include <drivers/device/i2c.h>

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

#include <nuttx/wqueue.h>
#include <nuttx/clock.h>

#include <systemlib/perf_counter.h>
#include <systemlib/err.h>
#include <systemlib/systemlib.h>
#include <systemlib/mixer/mixer.h>
#include <systemlib/pwm_limit/pwm_limit.h>

#include <drivers/drv_pwm_output.h>

#include <uORB/uORB.h>
#include <uORB/topics/actuator_controls.h>
#include <uORB/topics/actuator_outputs.h>
#include <uORB/topics/actuator_armed.h>

#include <board_config.h>
#include <drivers/drv_io_expander.h>

#define PCA9685_SUBADR1 0x2
#define PCA9685_SUBADR2 0x3
#define PCA9685_SUBADR3 0x4

#define PCA9685_MODE1 0x0
#define PCA9685_PRESCALE 0xFE

#define LED0_ON_L 0x6
#define LED0_ON_H 0x7
#define LED0_OFF_L 0x8
#define LED0_OFF_H 0x9

#define ALLLED_ON_L 0xFA
#define ALLLED_ON_H 0xFB
#define ALLLED_OFF_L 0xFC
#define ALLLED_OF

#define ADDR 0x40	// I2C adress

#define PCA9685_DEVICE_PATH "/dev/pca9685"
#define PCA9685_BUS PX4_I2C_BUS_EXPANSION
#define PCA9685_PWMFREQ 60.0f
#define PCA9685_NCHANS 16 // total amount of pwm outputs

#define PCA9685_PWMMIN 150 // this is the 'minimum' pulse length count (out of 4096)
#define PCA9685_PWMMAX 600 // this is the 'maximum' pulse length count (out of 4096)_PWMFREQ 60.0f

#define PCA9685_PWMCENTER ((PCA9685_PWMMAX + PCA9685_PWMMIN)/2)
#define PCA9685_MAXSERVODEG 180.0f /* maximal servo deflection in degrees
				     PCA9685_PWMMIN <--> -PCA9685_MAXSERVODEG
				     PCA9685_PWMMAX <--> PCA9685_MAXSERVODEG
				     */
#define PCA9685_SCALE ((PCA9685_PWMMAX - PCA9685_PWMCENTER)/(M_DEG_TO_RAD_F * PCA9685_MAXSERVODEG)) // scales from rad to PWM

#define NAN_VALUE	(0.0f/0.0f)


class PCA9685 : public device::I2C
{
public:
	PCA9685(int bus = PCA9685_BUS, uint8_t address = ADDR);
	virtual ~PCA9685();


	virtual int		init();
	virtual int		ioctl(struct file *filp, int cmd, unsigned long arg);
	virtual int		info();
	virtual int		reset();
	bool			is_running() { return _running; }

	void 					getActuation(struct actuator_controls_s* act, uint16_t offset);
	void 					getMotors(uint16_t* motors);
	bool 					MixerInit();

private:
	work_s			_work;


	enum IOX_MODE		_mode;
	bool			_running;
	int			_i2cpwm_interval;
	bool			_should_run;
	perf_counter_t		_comms_errors;

	uint8_t			_msg[6];

	int							_control_subs[actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS];
	orb_id_t				_control_topics[actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS];
	struct actuator_controls_s _controls[actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS];
	float 					_outputs[actuator_outputs_s::NUM_ACTUATOR_OUTPUTS];
	uint16_t 				_rates[actuator_outputs_s::NUM_ACTUATOR_OUTPUTS];
	pollfd					_poll_fds[actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS];
	unsigned				_poll_fds_num;

	int 						_armed_sub;

	bool						_servo_armed;

	MixerGroup* 		_mixers;
	uint32_t				_groups_required;
	uint32_t 				_groups_subscribed;

  static actuator_armed_s	_armed;
	static pwm_limit_t	_pwm_limit;

	bool _mode_on_initialized;  /** Set to true after the first call of i2cpwm in mode IOX_MODE_ON */

	static bool	arm_nothrottle() { return (_armed.prearmed && !_armed.armed); }

	static void		i2cpwm_trampoline(void *arg);
	void			i2cpwm();

	void 			subscribe();

	/**
	 * Helper function to set the pwm frequency
	 */
	int setPWMFreq(float freq);

	/**
	 * Helper function to set the demanded pwm value
	 * @param num pwm output number
	 */
	int setPWM(uint8_t num, uint16_t on, uint16_t off);

	/**
	 * Sets pin without having to deal with on/off tick placement and properly handles
	 * a zero value as completely off.  Optional invert parameter supports inverting
	 * the pulse for sinking to ground.
	 * @param num pwm output number
	 * @param val should be a value from 0 to 4095 inclusive.
	 */
	int setPin(uint8_t num, uint16_t val, bool invert = false);


	/* Wrapper to read a byte from addr */
	int read8(uint8_t addr, uint8_t &value);

	/* Wrapper to wite a byte to addr */
	int write8(uint8_t addr, uint8_t value);

	static int	control_callback(uintptr_t handle,
					 uint8_t control_group,
					 uint8_t control_index,
					 float &input);

};

actuator_armed_s	PCA9685::_armed = {};
pwm_limit_t		PCA9685::_pwm_limit;

/* for now, we only support one board */
namespace
{
PCA9685 *g_pca9685;
}

void pca9685_usage();

extern "C" __EXPORT int pca9685_main(int argc, char *argv[]);

PCA9685::PCA9685(int bus, uint8_t address) :
	I2C("pca9685", PCA9685_DEVICE_PATH, bus, address, 100000),
	_mode(IOX_MODE_OFF),
	_running(false),
	_i2cpwm_interval(SEC2TICK(1.0f / 60.0f)),
	_should_run(false),
	_comms_errors(perf_alloc(PC_COUNT, "actuator_controls_1_comms_errors")),
	_control_subs{-1},
	_poll_fds_num(0),
	_armed_sub(-1),
	_servo_armed(false),
	_mixers(nullptr),
	_groups_required(0),
	_groups_subscribed(0),
	_mode_on_initialized(false)
{

	_control_topics[0] = ORB_ID(actuator_controls_0);
	_control_topics[1] = ORB_ID(actuator_controls_1);
	_control_topics[2] = ORB_ID(actuator_controls_2);
	_control_topics[3] = ORB_ID(actuator_controls_3);

	memset(&_work, 0, sizeof(_work));
	memset(_msg, 0, sizeof(_msg));
	memset(_controls, 0, sizeof(_controls));
	memset(_poll_fds, 0, sizeof(_poll_fds));
	memset(_outputs, 0, sizeof(_outputs));
	memset(_rates, 0, sizeof(_rates));
}

PCA9685::~PCA9685()
{
}

int
PCA9685::init()
{
	int ret;
	ret = I2C::init();

	if (ret != OK) {
		return ret;
	}

	ret = reset();

	if (ret != OK) {
		return ret;
	}

	ret = setPWMFreq(PCA9685_PWMFREQ);

	return ret;
}

void
PCA9685::getActuation(struct actuator_controls_s* act, uint16_t offset) {
	if (offset < actuator_controls_s::NUM_ACTUATOR_CONTROLS) {
		memcpy(act, &_controls[offset], sizeof(actuator_controls_s));
	}
}

bool
PCA9685::MixerInit() {
	return _mixers != nullptr;
}

void
PCA9685::getMotors(uint16_t* motors) {
	for (size_t i = 0; i < actuator_outputs_s::NUM_ACTUATOR_OUTPUTS; ++i) {
		motors[i] = _rates[i];
	}
}

void
PCA9685::subscribe()
{
	/* subscribe/unsubscribe to required actuator control groups */
	uint32_t sub_groups = _groups_required & ~_groups_subscribed;
	uint32_t unsub_groups = _groups_subscribed & ~_groups_required;
	_poll_fds_num = 0;

	for (unsigned i = 0; i < actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS; i++) {
		if (sub_groups & (1 << i)) {
			DEVICE_DEBUG("subscribe to actuator_controls_%d", i);
			_control_subs[i] = orb_subscribe(_control_topics[i]);
		}

		if (unsub_groups & (1 << i)) {
			DEVICE_DEBUG("unsubscribe from actuator_controls_%d", i);
			::close(_control_subs[i]);
			_control_subs[i] = -1;
		}

		if (_control_subs[i] > 0) {
			_poll_fds[_poll_fds_num].fd = _control_subs[i];
			_poll_fds[_poll_fds_num].events = POLLIN;
			_poll_fds_num++;
		}
	}
}

int
PCA9685::control_callback(uintptr_t handle,
			 uint8_t control_group,
			 uint8_t control_index,
			 float &input)
{
	const actuator_controls_s *controls = (actuator_controls_s *)handle;

	input = controls[control_group].control[control_index];

	/* limit control input */
	if (input > 1.0f) {
		input = 1.0f;

	} else if (input < -1.0f) {
		input = -1.0f;
	}

	return 0;
}

int
PCA9685::ioctl(struct file *filp, int cmd, unsigned long arg)
{
	int ret = -EINVAL;

	switch (cmd) {

		case MIXERIOCRESET:
			if (_mixers != nullptr) {
				delete _mixers;
				_mixers = nullptr;
				_groups_required = 0;
			}
			ret = OK;
			break;

		case MIXERIOCADDSIMPLE: {
			mixer_simple_s *mixinfo = (mixer_simple_s *)arg;

			SimpleMixer *mixer = new SimpleMixer(control_callback,
				(uintptr_t)_controls, mixinfo);

			if (mixer->check()) {
				delete mixer;
				_groups_required = 0;
				ret = -EINVAL;
			} else {
				if (_mixers == nullptr)
					_mixers = new MixerGroup(control_callback,
						(uintptr_t)_controls);

				_mixers->add_mixer(mixer);
				_mixers->groups_required(_groups_required);
				ret = OK;
			}

			break;
		}

		case MIXERIOCLOADBUF: {
			const char *buf = (const char *)arg;
			unsigned buflen = strnlen(buf, 1024);

			if (_mixers == nullptr) {
				_mixers = new MixerGroup(control_callback,
					(uintptr_t)_controls);
			}

			if (_mixers == nullptr) {
				_groups_required = 0;
				ret = -ENOMEM;
			} else {
				ret = _mixers->load_from_buf(buf, buflen);

				if (ret != 0) {
					DEVICE_DEBUG("mixer load failed with %d", ret);
					delete _mixers;
					_mixers = nullptr;
					_groups_required = 0;
					ret = -EINVAL;
				} else {
					_mixers->groups_required(_groups_required);
					ret = OK;
				}
			}

			break;
		}

	case IOX_SET_MODE:

		if (_mode != (IOX_MODE)arg) {

			switch ((IOX_MODE)arg) {
			case IOX_MODE_OFF:
				warnx("shutting down");
				break;

			case IOX_MODE_ON:
				warnx("starting");
				break;

			case IOX_MODE_TEST_OUT:
				warnx("test starting");
				break;

			default:
				return -1;
			}

			_mode = (IOX_MODE)arg;
		}

		// if not active, kick it
		if (!_running) {
			_running = true;
			work_queue(LPWORK, &_work, (worker_t)&PCA9685::i2cpwm_trampoline, this, 1);
		}


		return OK;

	default:
		// see if the parent class can make any use of it
		ret = CDev::ioctl(filp, cmd, arg);
		break;
	}

	return ret;
}

int
PCA9685::info()
{
	int ret = OK;

	if (is_running()) {
		warnx("Driver is running, mode: %u", _mode);

	} else {
		warnx("Driver started but not running");
	}

	return ret;
}

void
PCA9685::i2cpwm_trampoline(void *arg)
{
	PCA9685 *i2cpwm = reinterpret_cast<PCA9685 *>(arg);

	i2cpwm->i2cpwm();
}

/**
 * Main loop function
 */
void
PCA9685::i2cpwm()
{
	if (_mode == IOX_MODE_TEST_OUT) {
		setPin(0, PCA9685_PWMCENTER);
		_should_run = true;

	} else if (_mode == IOX_MODE_OFF) {
		_should_run = false;

	} else {
		if (!_mode_on_initialized) {
			// Init PWM limits
			pwm_limit_init(&_pwm_limit);

			// Get arming state
			_armed_sub = orb_subscribe(ORB_ID(actuator_armed));

			/* Subscribe to actuator groups */
			subscribe();

			/* set the uorb update interval lower than the driver pwm interval */
			for (unsigned i = 0; i < actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS; ++i) {
				orb_set_interval(_control_subs[i], 1000.0f / PCA9685_PWMFREQ - 5);
			}

			_mode_on_initialized = true;
		}

		/* check if anything updated */
		int ret = ::poll(_poll_fds, _poll_fds_num, 0);

		if (ret < 0) {
			DEVICE_LOG("poll error %d", errno);

		} else if (ret == 0) {
	//			warnx("no PWM: failsafe");
		} else {
			/* get controls for required topics */
			unsigned poll_id = 0;

			for (unsigned i = 0; i < actuator_controls_s::NUM_ACTUATOR_CONTROL_GROUPS; i++) {
				if (_control_subs[i] > 0) {
					if (_poll_fds[poll_id].revents & POLLIN) {
						orb_copy(_control_topics[i], _control_subs[i], &_controls[i]);
					}
				}
				poll_id++;
			}

			if (_mixers != nullptr) {
				size_t num_outputs = actuator_outputs_s::NUM_ACTUATOR_OUTPUTS;

				// do mixing
				num_outputs = _mixers->mix(_outputs, num_outputs, NULL);

				// disable unused ports by setting their output to NaN
				for (size_t i = 0; i < sizeof(_outputs) / sizeof(_outputs[0]); i++) {
					if (i >= num_outputs) {
						_outputs[i] = NAN_VALUE;
					}
				}

				// Finally, write servo values to motors
				for (int i = 0; i < num_outputs; i++) {
					uint16_t new_value = PCA9685_PWMCENTER +
								 (_outputs[i] * M_PI_F * PCA9685_SCALE);
					// DEVICE_DEBUG("%d: current: %u, new %u, control %.2f", i, _current_values[i], new_value,
					//      (double)_controls[1].control[i]);

					if (isfinite(new_value) &&
						new_value >= PCA9685_PWMMIN &&
						new_value <= PCA9685_PWMMAX) {

						setPin(i, new_value);
						_rates[i] = new_value;
					}
				}
			}
		}

		bool updated;

		// Update Arming state
		orb_check(_armed_sub, &updated);
		if (updated) {
			orb_copy(ORB_ID(actuator_armed), _armed_sub, &_armed);

			bool set_armed = (_armed.armed || _armed.prearmed) && !_armed.lockdown;

			if (_servo_armed != set_armed) {
				_servo_armed = set_armed;
			}
		}

		// Update AUX controls update
		// orb_check(_actuator_controls_sub, &updated);
		// if (updated) {
		// 	size_t num_outputs = actuator_outputs_s::NUM_ACTUATOR_OUTPUTS;
		//
		// 	// Get updated actuator
		// 	// Only update actuator 1 for now
		// 	orb_copy(ORB_ID(actuator_controls_1), _actuator_controls_sub, &_controls[1]);
		//
		//
		// }

		_should_run = true;
	}

	// check if any activity remains, else stop
	if (!_should_run) {
		_running = false;
		return;
	}

	// re-queue ourselves to run again later
	_running = true;
	work_queue(LPWORK, &_work, (worker_t)&PCA9685::i2cpwm_trampoline, this, _i2cpwm_interval);
}

int
PCA9685::setPWM(uint8_t num, uint16_t on, uint16_t off)
{
	int ret;
	/* convert to correct message */
	_msg[0] = LED0_ON_L + 4 * num;
	_msg[1] = on;
	_msg[2] = on >> 8;
	_msg[3] = off;
	_msg[4] = off >> 8;

	/* try i2c transfer */
	ret = transfer(_msg, 5, nullptr, 0);

	if (OK != ret) {
		perf_count(_comms_errors);
		DEVICE_LOG("i2c::transfer returned %d", ret);
	}

	return ret;
}

int
PCA9685::setPin(uint8_t num, uint16_t val, bool invert)
{
	// Clamp value between 0 and 4095 inclusive.
	if (val > 4095) {
		val = 4095;
	}

	if (invert) {
		if (val == 0) {
			// Special value for signal fully on.
			return setPWM(num, 4096, 0);

		} else if (val == 4095) {
			// Special value for signal fully off.
			return setPWM(num, 0, 4096);

		} else {
			return setPWM(num, 0, 4095 - val);
		}

	} else {
		if (val == 4095) {
			// Special value for signal fully on.
			return setPWM(num, 4096, 0);

		} else if (val == 0) {
			// Special value for signal fully off.
			return setPWM(num, 0, 4096);

		} else {
			return setPWM(num, 0, val);
		}
	}

	return PX4_ERROR;
}

int
PCA9685::setPWMFreq(float freq)
{
	int ret  = OK;
	freq *= 0.9f;  /* Correct for overshoot in the frequency setting (see issue
		https://github.com/adafruit/Adafruit-PWM-Servo-Driver-Library/issues/11). */
	float prescaleval = 25000000;
	prescaleval /= 4096;
	prescaleval /= freq;
	prescaleval -= 1;
	uint8_t prescale = uint8_t(prescaleval + 0.5f); //implicit floor()
	uint8_t oldmode;
	ret = read8(PCA9685_MODE1, oldmode);

	if (ret != OK) {
		return ret;
	}

	uint8_t newmode = (oldmode & 0x7F) | 0x10; // sleep

	ret = write8(PCA9685_MODE1, newmode); // go to sleep

	if (ret != OK) {
		return ret;
	}

	ret = write8(PCA9685_PRESCALE, prescale); // set the prescaler

	if (ret != OK) {
		return ret;
	}

	ret = write8(PCA9685_MODE1, oldmode);

	if (ret != OK) {
		return ret;
	}

	usleep(5000); //5ms delay (from arduino driver)

	ret = write8(PCA9685_MODE1, oldmode | 0xa1);  //  This sets the MODE1 register to turn on auto increment.

	if (ret != OK) {
		return ret;
	}

	return ret;
}

/* Wrapper to read a byte from addr */
int
PCA9685::read8(uint8_t addr, uint8_t &value)
{
	int ret = OK;

	/* send addr */
	ret = transfer(&addr, sizeof(addr), nullptr, 0);

	if (ret != OK) {
		goto fail_read;
	}

	/* get value */
	ret = transfer(nullptr, 0, &value, 1);

	if (ret != OK) {
		goto fail_read;
	}

	return ret;

fail_read:
	perf_count(_comms_errors);
	DEVICE_LOG("i2c::transfer returned %d", ret);

	return ret;
}

int PCA9685::reset(void)
{
	warnx("resetting");
	return write8(PCA9685_MODE1, 0x0);
}

/* Wrapper to wite a byte to addr */
int
PCA9685::write8(uint8_t addr, uint8_t value)
{
	int ret = OK;
	_msg[0] = addr;
	_msg[1] = value;
	/* send addr and value */
	ret = transfer(_msg, 2, nullptr, 0);

	if (ret != OK) {
		perf_count(_comms_errors);
		DEVICE_LOG("i2c::transfer returned %d", ret);
	}

	return ret;
}

void
pca9685_usage()
{
	warnx("missing command: try 'start', 'test', 'stop', 'info'");
	warnx("options:");
	warnx("    -b i2cbus (%d)", PX4_I2C_BUS_EXPANSION);
	warnx("    -a addr (0x%x)", ADDR);
}

int
pca9685_main(int argc, char *argv[])
{
	int i2cdevice = -1;
	int i2caddr = ADDR; // 7bit

	int ch;

	// jump over start/off/etc and look at options first
	while ((ch = getopt(argc, argv, "a:b:")) != EOF) {
		switch (ch) {
		case 'a':
			i2caddr = strtol(optarg, NULL, 0);
			break;

		case 'b':
			i2cdevice = strtol(optarg, NULL, 0);
			break;

		default:
			pca9685_usage();
			exit(0);
		}
	}

	if (optind >= argc) {
		pca9685_usage();
		exit(1);
	}

	const char *verb = argv[optind];

	int fd;
	int ret;

	if (!strcmp(verb, "start")) {
		if (g_pca9685 != nullptr) {
			errx(1, "already started");
		}

		if (i2cdevice == -1) {
			// try the external bus first
			i2cdevice = PX4_I2C_BUS_EXPANSION;
			g_pca9685 = new PCA9685(PX4_I2C_BUS_EXPANSION, i2caddr);

			if (g_pca9685 != nullptr && OK != g_pca9685->init()) {
				delete g_pca9685;
				g_pca9685 = nullptr;
			}

			if (g_pca9685 == nullptr) {
				errx(1, "init failed");
			}
		}

		if (g_pca9685 == nullptr) {
			g_pca9685 = new PCA9685(i2cdevice, i2caddr);

			if (g_pca9685 == nullptr) {
				errx(1, "new failed");
			}

			if (OK != g_pca9685->init()) {
				delete g_pca9685;
				g_pca9685 = nullptr;
				errx(1, "init failed");
			}
		}

		fd = open(PCA9685_DEVICE_PATH, 0);

		if (fd == -1) {
			errx(1, "Unable to open " PCA9685_DEVICE_PATH);
		}

		ret = ioctl(fd, IOX_SET_MODE, (unsigned long)IOX_MODE_ON);
		close(fd);


		exit(0);
	}

	// need the driver past this point
	if (g_pca9685 == nullptr) {
		warnx("not started, run pca9685 start");
		exit(1);
	}

	if (!strcmp(verb, "info")) {
		g_pca9685->info();
		exit(0);
	}

	if (!strcmp(verb, "reset")) {
		g_pca9685->reset();
		exit(0);
	}

	if (!strcmp(verb, "status")) {
		if (g_pca9685 != nullptr) {
			int i;
			struct actuator_controls_s actuation;
			uint16_t servoVals[actuator_outputs_s::NUM_ACTUATOR_OUTPUTS];

			if (g_pca9685->MixerInit()) {
				printf("Mixer initialized.\n");
			} else {
				printf("Mixer not initialized.\n");
			}

			g_pca9685->getActuation(&actuation, 1);
			printf("Actuator Group 1 Status\n");
			for (i = 0; i < actuator_controls_s::NUM_ACTUATOR_CONTROLS; ++i) {
				double val = actuation.control[i];
				printf("Act %d: %2.6f\n", i, val);
			}

			printf("\n");
			g_pca9685->getMotors((uint16_t*)&servoVals);
			printf("Raw Servos\n");
			for (i = 0; i < actuator_outputs_s::NUM_ACTUATOR_OUTPUTS; ++i) {
				uint16_t val = servoVals[i];
				printf("Servo %d: %d\n", i, val);
			}

			exit(0);
		} else {
			warnx("PCA9685 isn't running.");
			exit(1);
		}
	}


	if (!strcmp(verb, "test")) {
		fd = open(PCA9685_DEVICE_PATH, 0);

		if (fd == -1) {
			errx(1, "Unable to open " PCA9685_DEVICE_PATH);
		}

		ret = ioctl(fd, IOX_SET_MODE, (unsigned long)IOX_MODE_TEST_OUT);

		close(fd);
		exit(ret);
	}

	if (!strcmp(verb, "stop")) {
		fd = open(PCA9685_DEVICE_PATH, 0);

		if (fd == -1) {
			errx(1, "Unable to open " PCA9685_DEVICE_PATH);
		}

		ret = ioctl(fd, IOX_SET_MODE, (unsigned long)IOX_MODE_OFF);
		close(fd);

		// wait until we're not running any more
		for (unsigned i = 0; i < 15; i++) {
			if (!g_pca9685->is_running()) {
				break;
			}

			usleep(50000);
			printf(".");
			fflush(stdout);
		}

		printf("\n");
		fflush(stdout);

		if (!g_pca9685->is_running()) {
			delete g_pca9685;
			g_pca9685 = nullptr;
			warnx("stopped, exiting");
			exit(0);

		} else {
			warnx("stop failed.");
			exit(1);
		}
	}

	pca9685_usage();
	exit(0);
}
