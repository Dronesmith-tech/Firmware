#ifndef TURTLENAV_H_
#define TURTLENAV_H_

#include <systemlib/perf_counter.h>

#include <controllib/blocks.hpp>
#include <controllib/block/BlockParam.hpp>
#include <navigator/navigation.h>

#include <uORB/uORB.h>
#include <uORB/topics/mission.h>
#include <uORB/topics/vehicle_control_mode.h>
#include <uORB/topics/position_setpoint_triplet.h>
#include <uORB/topics/vehicle_local_position.h>
#include <uORB/topics/vehicle_gps_position.h>
#include <uORB/topics/parameter_update.h>
#include <uORB/topics/mission_result.h>
#include <uORB/topics/geofence_result.h>
#include <uORB/topics/vehicle_attitude_setpoint.h>
#include <uORB/topics/vehicle_land_detected.h>
#include <uORB/topics/sensor_combined.h>


typedef enum {
  TURTLECMD_HOLD = 0,
  TURTLECMD_TAKEOFF,
  TURTLECMD_LAND,
  TURTLECMD_LEFT,
  TURTLECMD_RIGHT,
  TURTLECMD_DOWN,
  TURTLECMD_UP,
  TURTLECMD_FRONT,
  TURTLECMD_BACK,
  TURTLECMD_YAW
} TurtleCmd;

class TurtleNav
{
public:
	/**
	 * Constructor
	 */
	TurtleNav();

	/**
	 * Destructor, also kills the turtle task.
	 */
	~TurtleNav();

	/**
	 * Start the turtle task.
	 *
	 * @return		OK on success.
	 */
	int		start();

	/**
	 * Display the turtle status.
	 */
	void		status();

	/**
	 * Setters
	 */
  void		set_can_loiter_at_sp(bool can_loiter) { _can_loiter_at_sp = can_loiter; }
  void		set_position_setpoint_triplet_updated() { _pos_sp_triplet_updated = true; }
  void    setTurtleCmd(TurtleCmd t) { _turtle_cmd = t; }

	/**
	 * Getters
	 */
  struct vehicle_local_position_s*   get_local_position() { return &_local_pos; }
  struct position_setpoint_triplet_s* get_takeoff_triplet() { return &_takeoff_triplet; }
  struct position_setpoint_triplet_s* get_reposition_triplet() { return &_reposition_triplet; }
  struct position_setpoint_triplet_s* get_position_setpoint_triplet() { return &_pos_sp_triplet; }

private:

	bool	_task_should_exit;		  /**< if true, sensor task should exit */
	int		_turtlenav_task;		    /**< task handle for sensor task */

  orb_advert_t	_mavlink_log_pub;		/**< the uORB advert to send messages over mavlink */

	perf_counter_t	_loop_perf;		/**< loop performance counter */

  int		_local_pos_sub;		      /**< local position subscription */
	int		_sensor_combined_sub;		/**< sensor combined subscription */
	int		_home_pos_sub;			    /**< home position subscription */
	int		_vstatus_sub;			      /**< vehicle status subscription */
	int		_land_detected_sub;		  /**< vehicle land detected subscription */
	int		_control_mode_sub;	    /**< vehicle control mode subscription */
	int		_param_update_sub;	    /**< param update subscription */
	int		_vehicle_command_sub;	  /**< vehicle commands (onboard and offboard) */

  orb_advert_t	_pos_sp_triplet_pub;		/**< publish position setpoint triplet */

  vehicle_status_s				    _vstatus;		       /**< vehicle status */
	vehicle_land_detected_s		  _land_detected;		 /**< vehicle land_detected */
	vehicle_control_mode_s			_control_mode;	   /**< vehicle control mode */
	vehicle_local_position_s		_local_pos;		     /**< local vehicle position */
	sensor_combined_s				    _sensor_combined;	 /**< sensor values */
	home_position_s					    _home_pos;		     /**< home position for RTL */

	position_setpoint_triplet_s			_pos_sp_triplet;	    /**< triplet of position setpoints */
	position_setpoint_triplet_s			_reposition_triplet;	/**< triplet for non-mission direct position command */
	position_setpoint_triplet_s			_takeoff_triplet;	    /**< triplet for non-mission direct takeoff command */

  bool		_can_loiter_at_sp;			                      /**< flags if current position SP can be used to loiter */
	bool		_pos_sp_triplet_updated;		                  /**< flags if position SP triplet needs to be published */
	bool 		_pos_sp_triplet_published_invalid_once;	      /**< flags if position SP triplet has been published once to UORB */

  TurtleCmd _turtle_cmd;

  bool    inRange();

  void    updateCmd();

  /**
   * Retrieve local position
   */
  void		local_position_update();

  /**
   * Retrieve sensor values
   */
  void		sensor_combined_update();

  /**
   * Retrieve home position
   */
  void		home_position_update(bool force=false);

  /**
   * Retrieve vehicle status
   */
  void		vehicle_status_update();

  /**
   * Retrieve vehicle land detected
   */
  void		vehicle_land_detected_update();

  /**
   * Retrieve vehicle control mode
   */
  void		vehicle_control_mode_update();

  /**
   * Update parameters
   */
  void		params_update();

  /**
   * Publish a new position setpoint triplet for position controllers
   */
  void		publish_position_setpoint_triplet();

  //
  // Update mission
  //
  void    updateMission();

  //
  // Update landing
  //
  void    updateLand();

  //
  // Update takeoff
  //
  void    updateTakeOff();

  //
  // Update loiter
  //
  void    updateLoiter();

	/**
	 * Shim for calling task_main from task_create.
	 */
	static void	task_main_trampoline(int argc, char *argv[]);

	/**
	 * Main task.
	 */
	void		task_main();

  /**
	 * this class has ptr data members, so it should not be copied,
	 * consequently the copy constructors are private.
	 */
	TurtleNav(const TurtleNav&);
	TurtleNav operator=(const TurtleNav&);
};

#endif /* TURTLENAV_H_ */
