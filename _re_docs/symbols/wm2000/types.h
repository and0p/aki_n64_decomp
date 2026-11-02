/*
 * WWF WrestleMania 2000 (US, V1.2) - structures recovered from the original game.
 *
 * Reverse-engineering reference only, not the port's own types. Pointers are 32-bit on
 * the N64; the IDA build adds __ptr32 to every pointer automatically.
 *
 * Only the basic types for now: the World Tour / Revenge structs are not copied in, because
 * their layouts differ between the games. Add each struct here once its layout is read from
 * No Mercy's own code. Field comments follow ../wcw/types.h ("?" marks a guess).
 */
typedef unsigned char u8; typedef signed char s8;
typedef unsigned short u16; typedef short s16;
typedef unsigned int u32; typedef int s32; typedef float f32;

struct Vec3 { f32 x, y, z; };
struct Vec4 { f32 x, y, z, w; };
