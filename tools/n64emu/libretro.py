"""Minimal libretro frontend core wrapper (ctypes), enough for mupen64plus_next.

Loads a core DLL, answers the environment calls it needs, runs one video frame per run(),
keeps the last video frame, feeds joypad input, and exposes save states and system RAM.
"""
import ctypes as C
import os

# Environment commands (libretro.h).
ENV_GET_CAN_DUPE = 3
ENV_SET_PERFORMANCE_LEVEL = 8
ENV_GET_SYSTEM_DIRECTORY = 9
ENV_SET_PIXEL_FORMAT = 10
ENV_SET_HW_RENDER = 14
ENV_GET_VARIABLE = 15
ENV_SET_VARIABLES = 16
ENV_GET_VARIABLE_UPDATE = 17
ENV_GET_LOG_INTERFACE = 27
ENV_GET_SAVE_DIRECTORY = 31
ENV_GET_CORE_OPTIONS_VERSION = 52

PIXEL_0RGB1555, PIXEL_XRGB8888, PIXEL_RGB565 = 0, 1, 2
MEMORY_SYSTEM_RAM = 2
DEVICE_JOYPAD, DEVICE_ANALOG = 1, 5


class retro_game_info(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]


class retro_system_info(C.Structure):
    _fields_ = [("library_name", C.c_char_p), ("library_version", C.c_char_p),
                ("valid_extensions", C.c_char_p), ("need_fullpath", C.c_bool), ("block_extract", C.c_bool)]


class retro_game_geometry(C.Structure):
    _fields_ = [("base_width", C.c_uint), ("base_height", C.c_uint), ("max_width", C.c_uint),
                ("max_height", C.c_uint), ("aspect_ratio", C.c_float)]


class retro_system_timing(C.Structure):
    _fields_ = [("fps", C.c_double), ("sample_rate", C.c_double)]


class retro_system_av_info(C.Structure):
    _fields_ = [("geometry", retro_game_geometry), ("timing", retro_system_timing)]


class retro_variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


ENV_CB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO_CB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIO_BATCH_CB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL_CB = C.CFUNCTYPE(None)
STATE_CB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
LOG_CB = C.CFUNCTYPE(None, C.c_int, C.c_char_p)  # really variadic; the format string is enough


class Core:
    def __init__(self, dll_path, options, system_dir, save_dir, log=None):
        self.dll = C.CDLL(os.path.abspath(dll_path))
        self.options = {k.encode(): v.encode() for k, v in options.items()}
        self._strings = []  # keep returned C strings alive
        self.system_dir = C.c_char_p(os.path.abspath(system_dir).encode())
        self.save_dir = C.c_char_p(os.path.abspath(save_dir).encode())
        self.log = log or (lambda s: None)
        self.pixel_format = PIXEL_0RGB1555
        self.frame = None  # (width, height, pitch, bytes) of the last video frame
        self.video_frames = 0
        self.buttons = [0] * 4  # RetroPad joypad bits per port (bit = id)
        self.analog = [[0, 0, 0, 0] for _ in range(4)]  # lx, ly, rx, ry per port

        self._env_cb = ENV_CB(self._env)
        self._video_cb = VIDEO_CB(self._video)
        self._audio_cb = AUDIO_CB(lambda l, r: None)
        self._audio_batch_cb = AUDIO_BATCH_CB(lambda data, frames: frames)
        self._poll_cb = POLL_CB(lambda: None)
        self._state_cb = STATE_CB(self._input_state)
        self._log_cb = LOG_CB(lambda level, fmt: self.log((fmt or b"").decode(errors="replace").rstrip()))
        self._log_iface = (C.c_void_p * 1)(C.cast(self._log_cb, C.c_void_p))

        d = self.dll
        d.retro_set_environment(self._env_cb)
        d.retro_set_video_refresh(self._video_cb)
        d.retro_set_audio_sample(self._audio_cb)
        d.retro_set_audio_sample_batch(self._audio_batch_cb)
        d.retro_set_input_poll(self._poll_cb)
        d.retro_set_input_state(self._state_cb)
        d.retro_serialize_size.restype = C.c_size_t
        d.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        d.retro_serialize.restype = C.c_bool
        d.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        d.retro_unserialize.restype = C.c_bool
        d.retro_get_memory_data.restype = C.c_void_p
        d.retro_get_memory_size.restype = C.c_size_t
        d.retro_load_game.argtypes = [C.POINTER(retro_game_info)]
        d.retro_load_game.restype = C.c_bool
        d.retro_init()

    def _cstr(self, b):
        s = C.c_char_p(b)
        self._strings.append(s)
        return s

    def _env(self, cmd, data):
        cmd &= 0xFFFF  # drop the experimental/private flags
        if cmd == ENV_GET_CAN_DUPE:
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        if cmd == ENV_SET_PIXEL_FORMAT:
            self.pixel_format = C.cast(data, C.POINTER(C.c_int))[0]
            return True
        if cmd == ENV_GET_SYSTEM_DIRECTORY:
            C.cast(data, C.POINTER(C.c_char_p))[0] = self.system_dir
            return True
        if cmd == ENV_GET_SAVE_DIRECTORY:
            C.cast(data, C.POINTER(C.c_char_p))[0] = self.save_dir
            return True
        if cmd == ENV_GET_VARIABLE:
            var = C.cast(data, C.POINTER(retro_variable))[0]
            value = self.options.get(var.key)
            if value is None:
                return False
            C.cast(data, C.POINTER(retro_variable))[0].value = self._cstr(value)
            return True
        if cmd == ENV_GET_VARIABLE_UPDATE:
            C.cast(data, C.POINTER(C.c_bool))[0] = False
            return True
        if cmd == ENV_SET_VARIABLES:
            # Defaults are the first value of "Description; a|b|c"; keep ours where we set one.
            arr = C.cast(data, C.POINTER(retro_variable))
            i = 0
            while arr[i].key:
                key, desc = arr[i].key, arr[i].value or b""
                if key not in self.options and b"; " in desc:
                    self.options[key] = desc.split(b"; ", 1)[1].split(b"|")[0]
                i += 1
            return True
        if cmd == ENV_GET_LOG_INTERFACE:
            C.memmove(data, self._log_iface, C.sizeof(self._log_iface))
            return True
        if cmd == ENV_GET_CORE_OPTIONS_VERSION:
            C.cast(data, C.POINTER(C.c_uint))[0] = 0
            return True
        if cmd == ENV_SET_PERFORMANCE_LEVEL:
            return True
        return False

    def _video(self, data, width, height, pitch):
        self.video_frames += 1
        if data:  # NULL = duped frame
            self.frame = (width, height, pitch, C.string_at(data, pitch * height))

    def _input_state(self, port, device, index, id_):
        if port >= 4:
            return 0
        if device == DEVICE_JOYPAD:
            if id_ == 256:  # RETRO_DEVICE_ID_JOYPAD_MASK
                return self.buttons[port]
            return 1 if self.buttons[port] >> id_ & 1 else 0
        if device == DEVICE_ANALOG and index < 2 and id_ < 2:
            return self.analog[port][index * 2 + id_]
        return 0

    def system_info(self):
        info = retro_system_info()
        self.dll.retro_get_system_info(C.byref(info))
        return info

    def load_game(self, rom_path):
        data = open(rom_path, "rb").read()
        self._rom = C.create_string_buffer(data, len(data))
        info = retro_game_info(os.path.abspath(rom_path).encode(), C.cast(self._rom, C.c_void_p), len(data), None)
        if not self.dll.retro_load_game(C.byref(info)):
            raise RuntimeError("retro_load_game failed")
        av = retro_system_av_info()
        self.dll.retro_get_system_av_info(C.byref(av))
        self.av = av

    def run(self):
        self.dll.retro_run()

    def serialize(self):
        n = self.dll.retro_serialize_size()
        buf = C.create_string_buffer(n)
        if not self.dll.retro_serialize(buf, n):
            raise RuntimeError("retro_serialize failed")
        return buf.raw

    def unserialize(self, data):
        buf = C.create_string_buffer(data, len(data))
        if not self.dll.retro_unserialize(buf, len(data)):
            raise RuntimeError("retro_unserialize failed")

    def ram(self):
        """(address, size) of system RAM, as the core keeps it."""
        return self.dll.retro_get_memory_data(MEMORY_SYSTEM_RAM), self.dll.retro_get_memory_size(MEMORY_SYSTEM_RAM)

    def frame_rgb(self):
        """The last frame as (width, height, RGB bytes)."""
        if not self.frame:
            return None
        w, h, pitch, raw = self.frame
        out = bytearray(w * h * 3)
        if self.pixel_format == PIXEL_XRGB8888:
            px = raw if pitch == w * 4 else b"".join(raw[y * pitch:y * pitch + w * 4] for y in range(h))
            out[0::3], out[1::3], out[2::3] = px[2::4], px[1::4], px[0::4]
        else:
            for y in range(h):
                for x in range(w):
                    p = raw[y * pitch + x * 2] | raw[y * pitch + x * 2 + 1] << 8
                    if self.pixel_format == PIXEL_RGB565:
                        r, g, b = (p >> 11 & 31) << 3, (p >> 5 & 63) << 2, (p & 31) << 3
                    else:
                        r, g, b = (p >> 10 & 31) << 3, (p >> 5 & 31) << 3, (p & 31) << 3
                    o = (y * w + x) * 3
                    out[o:o + 3] = bytes((r, g, b))
        return w, h, bytes(out)
