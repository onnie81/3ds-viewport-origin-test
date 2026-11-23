# Viewport origin test

A 3DS homebrew that scrolls a picture by moving the viewport origin, to find out what the console does with a negative origin in `GPUREG_VIEWPORT_XY`.

`vporigin.3dsx` is in the [latest release](../../releases/latest).

An 800x480 picture sits behind the 400x240 top screen. The aim is to put the cross on the diamond; each hit moves the diamond. Left alone, it plays itself.

- **Mode A, origin scroll.** The viewport is 480x800 and the picture always fills it. Scrolling moves the viewport origin, which is 0,0 at the start and negative everywhere else. The scissor is the 240x400 framebuffer.
- **Mode B, projection scroll.** The viewport is 240x400 at 0,0 and the projection is shifted instead. This is the reference.

The bottom screen shows the origin and the value written to the register, for example `viewport 480x800 at x=-156 y=-172`, `GPUREG_VIEWPORT_XY = 0x03540364`.

## Reading the result

Switch between A and B with X at the same scroll position.

| On the console | Meaning |
|---|---|
| A and B look the same | A negative origin works: the field is sign-extended or the add wraps at 10 bits |
| A is the dark red clear colour with only the cross, except at the start | The field is plain unsigned |

## Controls

| | |
|---|---|
| D-pad or circle pad | scroll (switches to manual) |
| X | mode A / B |
| Y | automatic / manual |
| START | exit |

## Azahar

| Build | Mode A |
|---|---|
| 2126.2 (origin read as signed) | same as B |
| [PR #2844](https://github.com/azahar-emu/azahar/pull/2844) | same as B |
| origin read as unsigned | blank except at the start |

![Mode B and mode A on three Azahar builds](azahar.png)

## On a console

Nintendo 3DS XL (SPR-001): mode A shows the picture and matches mode B. A negative origin works on hardware.

Release the D-pad before taking a screenshot: while scrolling, the numbers on the bottom screen can be a few frames away from the picture.

## Building

devkitARM with libctru and citro3d:

```
make
```

gives `vporigin.3dsx`.
