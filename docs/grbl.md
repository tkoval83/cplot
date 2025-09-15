# GRBL's cornering algorithm
Contents

1.  [Theory](#theory)
2.  [Implementations](#implementations)

Cornering is how a machine tool head follows a path through a vertex at speed given a machine's physical constraints. Sonny Jeon's [article on improving GRBL's cornering algorithm](https://onehossshay.wordpress.com/2011/09/24/improving_grbl_cornering_algorithm/) describes a heuristic that keeps a machine's centripetal acceleration constant by approximating the corner with a circle. It's implemented in [GRBL's planner C code](https://github.com/grbl/grbl/blob/59a4d2ef06bc5ce09f6d38735f4638b1a63642e7/grbl/planner.c#L326) and widely copied by other motion planning systems.

GRBL is a G-code interpreter and receives high-level instructions for where to move the tool and converts them to low-level stepper motor movements. In contrast, EiBotBoard's "low-level move" commands and Klipper leave motion planning up to control software running on more powerful devices and communicate detailed moves to simple firmware. Regardless, any open source motion planning system I could find use Jeon's algorithm to lower speed going into corners.

Theory
------

This algorithm finds a cornering velocity based on the maximum acceleration the machine is capable of and a user-defined constant factor. In mathematical terms, cornering velocity `vc` with constant centripetal acceleration `a` around a circle of radius `r` is given by `vc = √(ar)`. This is derived from [analyzing the change in velocity for circular motion](https://pressbooks.online.ucf.edu/algphysics/chapter/centripetal-acceleration/).

The algorithm uses a circle that touches the edges of the corner and a deviation factor `δ` to control the minimum distance between the corner and the circle. A smaller `δ` translates to a smaller `r` and a proportionally smaller `vc`. Bisecting the angle `θ` of the two edges creates a right triangle with the opposite side having length `r` and the hypotenuse length `r + δ`. From trigonometry:

```
sin(θ / 2) = r / (r + δ)

```


And rearranging the terms to solve for `r`:

```
r = r sin(θ / 2) + δ sin(θ / 2)
r - r sin(θ / 2) = δ sin(θ / 2)
r (1 - sin(θ / 2)) = δ sin(θ / 2)
r = (δ sin(θ / 2)) / (1 - sin(θ / 2))

```


The angle `θ` is given by the dot product of the incoming edge `eᵢ` and the outgoing edge `eₒ`:

```
cos(θ) = (eᵢ ⋅ eₒ) / (|eᵢ| |eₒ|)

```


To avoid the expensive `cos⁻¹` and `sin` operations, Sonny used the half-angle identity for `sin`:

```
sin(θ / 2) = ± √(1 - cos(θ) / 2)

```


Where the sign of the expression is the sign of:

```
2π - θ + 4π ⌊θ / (4π)⌋

```


For an angle `θ` in `[0, 2π]`, the expression is always positive. Keeping the expression in terms of cosine allows substitution of the dot product, which is just a sum of products, which in 2-dimensions is:

```
eᵢ ⋅ eₒ = eᵢ.x eₒ.x + eᵢ.y eₒ.y

```


To compute the cosine:

```
cos(θ) = (eᵢ.x eₒ.x + eᵢ.y eₒ.y) / (|eᵢ| |eₒ|)

```


Combining the equations above to find `vc`:

```
c = √(1 - (((eᵢ.x eₒ.x + eᵢ.y eₒ.y) / (|eᵢ| |eₒ|)) / 2)
r = δ(c / (1 - c))
vc = √(aδ(c / (1 - c)))

```


This approach requires just two square root operations for each vertex. The division by the two vector's magnitudes are usually avoided by normalizing the vectors to the unit vectors.

The cornering velocity is used as the ending and starting velocities for the motion pieces leading to and from the vertex in [Constant acceleration motion planning](about:blank/Motion%20planning#Constant%20acceleration%20motion%20planning).

Implementations
---------------

GRBL's code is particularly terse, but benefits from thorough commenting. Instead of discrete operators for the vector components of motion, the operations are done incrementally, without any wasted steps.

Saxi overall has a more understandable structure to the code, with ASCII art comments to describe trapezoidal motion planning.

Klipper's code is accompanied by a [Kinematics document](https://www.klipper3d.org/Kinematics.html).

*   [GRBL in C for embedded devices](https://github.com/grbl/grbl/blob/59a4d2ef06bc5ce09f6d38735f4638b1a63642e7/grbl/planner.c#L326)

*   [saxi in TypeScript](https://github.com/nornagon/saxi/blob/main/src/planning.ts#L329)

*   [AxiDraw CLI in Python](https://github.com/evil-mad/axidraw/blob/eeb37e4501191ee508240a182e16da27a0832110/inkscape%20driver/axidraw.py#L1497)

*   [Klipper in Python](https://github.com/Klipper3d/klipper/blob/0d9b2cc1fa297b9adedb14be31a8d5c8d7868681/klippy/toolhead.py#L62)

*   [Michael Fogleman's motion module in Go](https://github.com/fogleman/motion/blob/master/util.go#L7)

*   [Michael Fogleman's axi in Python](https://github.com/fogleman/axi/blob/a5a12f01633076232be84a4e3321c80c7b9656b5/axi/planner.py#L152)
