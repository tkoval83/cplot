# Motion planning

[Pen plotters](https://mattwidmann.net/notes/pen-plotters) and [FDM 3D printers](https://en.wikipedia.org/wiki/Fused_filament_fabrication#Fused_deposition_modeling) take high-level representations of drawings and objects and convert them into physical objects by moving some tool head, like a pen or plastic extruder. To reconstruct the digital plans in the real world at speed, the machine needs to ensure that the tool head will accurately follow a path in spite of the inertia of the tool head. Motion planning ensures that quick changes in direction do not cause undue vibrations that impact the final product of the device.

Constant acceleration motion planning
-------------------------------------

In a graph of velocity over time, a constant acceleration motion plan that accelerates, cruises, and decelerates looks like a trapezoid. The acceleration of the machine should be limited to account for upcoming changes in direction. This requires decelerating to a safe velocity for the change in direction.

```
time:              ------------>

                      cruise
                      ______
velocity:            /      \
              accel /        \ -accel
                   /          \

                   --+
                     |
acceleration:      --+-----+
                           |
                           +--

```


To make this kind of motion efficient, the motor controller firmware needs to internally keep track of velocity and know which acceleration to apply.

Just how much velocity to bleed off going into a direction change is determined by algorithms that take into account the momentum of the tool and the physical constraints of the machine. All open source motion planners I've looked at that stick to constant acceleration profiles use [GRBL's cornering algorithm](https://mattwidmann.net/notes/grbl's-cornering-algorithm).

### AxiDraw specifics

[The AxiDraw pen plotter](https://mattwidmann.net/notes/the-axidraw-pen-plotter) uses a circuit board called the [EiBotBoard](https://www.schmalzhaus.com/EBB/) to control its motors. All motions on an EiBotBoard are in the units of stepper motor steps, because it doesn't know how the gearing of the stepper motors will correlate to physical movement. Furthermore, the on-board drivers support up to one-sixteenth micro-stepping, which reduces the movement by a factor of sixteen. The micro-stepping modes are less accurate and lower the holding torque of the stepper motors. On AxiDraw machines, each step corresponds to 0.2mm or, in other words, it takes 5 steps to move one millimeter.

AxiDraw machines are also mixed-axis geometries: the two stepper motors share a common belt that moves the gantry in both directions. To move the X-axis alone, both stepper motors need to move at the same rate. To move the Y-axis alone, the stepper motors need to move in exact opposite directions.

This leads to a straightforward conversion from movement in millimeters in the art board's axis to steps on each stepper motor:

```
steps_axis_1 = steps_x + steps_y
steps_axis_2 = steps_x - steps_y
steps_axis_1 = dist_mm_x * 5 + dist_mm_y * 5
steps_axis_2 = dist_mm_y * 5 - dist_mm_y * 5

```


### EiBotBoard's `LM` and `LT` commands

The EiBotBoard supports trapezoidal movement profiles with its low-level movement commands [`LM`](https://evil-mad.github.io/EggBot/ebb.html#LM) and [`LT`](https://evil-mad.github.io/EggBot/ebb.html#LT). These commands offer fine-grained control over a particular motion profile by independently controlling the acceleration of the stepper motors. [EiBotBoard GitHub Issue #73](https://github.com/evil-mad/EggBot/issues/73) describes the rationale for adding this command.1 1 These two commands are only fully-supported on EiBotBoard firmware versions 2.7.0 and later. There is limited support for `LM` on firmwares dating back to 2.5.0, but its functionality has slightly changed in intervening versions.

The `LM` command is a step-limited command; that is, it executes for a given number of steps. It has the following parameters for each axis:

*   The **rate** (unsigned 31-bit integer) is the starting rate of change in the stepper motor, which is effectively the velocity in one plane of motion.

*   The **steps** (signed 32-bit integer) is the number of steps to take on this axis. The sign controls the direction of the stepper motor.

*   The **acceleration** (signed 32-bit integer) is added to the rate value every 40µs.

*   An optional **clear** two-bit bitmask controls whether each stepper motor's accumulator is cleared at the start of the command.


The `LT` command is similar to `LM`, but instead of stopping at a number of steps for each axis, just stops after a set period of time.

To create a trapezoidal profile on an EiBotBoard, three separate commands must be issued:

1.  A low-level move command to accelerate up to a cruising speed according to the acceleration out of a change in direction.

2.  A cruising move at constant maximum velocity.

3.  A second low-level move to decelerate going into another change in direction.


For a path made up of many smaller line segments, like those found in the SVG `<path>` tag, this will involve a lot of tiny moves. With a Baud rate of 9600bps at 8-bits per character, the `LM` command can be up to 78 bytes long, allowing a rate of 18 commands-per-second.

Also, because two axes are varying their acceleration at the same time, non-linear movements are possible using these commands, but I haven't seen any software using them that way.

Constant jerk motion planning
-----------------------------

The third derivative of motion, jerk, goes to infinity when acceleration has a discontinuity as it flips between zero and non-zero at the edges of the trapezoid. This does correspond to a jerkiness in the motion with fast stepper motors and high weight in the tool heads, and can be addressed with varying acceleration motion plans, with the corners of the trapezoid rounded off, also known as "S-curve" profiles.

Several open source motion planning systems use constant jerk algorithms.

Pen plotters don't benefit much from these advanced profiles because of the limited momentum in the tool head. EiBotBoards don't even natively support variable acceleration movement commands.
