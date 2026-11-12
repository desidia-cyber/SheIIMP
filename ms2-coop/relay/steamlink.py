r"""Steam transport for the Mortal Shell II co-op relay.

Loads the flat Steamworks C API from the DLL the game already ships
(<game>\Engine\Binaries\ThirdParty\Steamworks\Steamv157\Win64\steam_api64.dll),
so nothing has to be downloaded or redistributed.

AppID 480 ("Spacewar") gives every Steam user lobbies + relayed P2P with no port
forwarding: the host creates a friends-only lobby, the friend joins it and the two
relays exchange state lines over ISteamNetworkingMessages.

Design notes taken from coop-anything's playbook/networking-steam-spacewar.md:
  * manual callback dispatch (no C++ callback classes)
  * SteamNetworkingIdentity is an opaque 136-byte buffer, only touched through
    SetSteamID64/GetSteamID64
  * SteamNetworkingMessage_t on x64: data at 0 (8-byte ptr), size at 8, sender at 16
  * a first message from an unknown SteamID raises SessionRequest (1251) and is held
    until AcceptSessionWithUser is called
  * send flags: Unreliable 0, NoNagle 1, Reliable 8
"""

import ctypes
import os
import struct
import sys
import time

CALLBACK_GAME_LOBBY_JOIN_REQUESTED = 333
CALLBACK_LOBBY_ENTER = 504
CALLBACK_LOBBY_CREATED = 513
CALLBACK_STEAM_API_CALL_COMPLETED = 703
CALLBACK_NET_SESSION_REQUEST = 1251
CALLBACK_NET_SESSION_FAILED = 1252

SEND_UNRELIABLE = 0
SEND_NONAGLE = 1
SEND_RELIABLE = 8

CHANNEL = 0
MAX_MESSAGES = 32
IDENTITY_SIZE = 136
MESSAGE_SIZE = 64          # generous: real struct on x64 is ~0x30-0x40


class CallbackMsg(ctypes.Structure):
    _fields_ = [
        ("hSteamUser", ctypes.c_int32),
        ("callback", ctypes.c_int32),
        ("param", ctypes.c_void_p),
        ("param_size", ctypes.c_int32),
    ]


class SteamAPICallCompleted(ctypes.Structure):
    _fields_ = [
        ("call", ctypes.c_uint64),
        ("callback", ctypes.c_int32),
        ("param_size", ctypes.c_uint32),
    ]


class LobbyCreated(ctypes.Structure):
    _fields_ = [("result", ctypes.c_int32), ("lobby", ctypes.c_uint64)]


class LobbyEnter(ctypes.Structure):
    _fields_ = [
        ("lobby", ctypes.c_uint64),
        ("perms", ctypes.c_uint32),
        ("locked", ctypes.c_bool),
        ("response", ctypes.c_uint32),
    ]


def find_steam_dll(game_dir=None):
    """Prefer the game's own DLL; fall back to the local folder."""
    cands = []
    if game_dir:
        cands.append(os.path.join(game_dir, "Engine", "Binaries", "ThirdParty",
                                  "Steamworks", "Steamv157", "Win64", "steam_api64.dll"))
    cands.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), "steam_api64.dll"))
    for c in cands:
        if os.path.exists(c):
            return c
    return None


class SteamLink:
    def __init__(self, role, player, lobby_id, game_dir, log):
        self.role = role
        self.player = player
        self.lobby_id = lobby_id
        self.log = log
        self.peer_ids = set()
        self.host_id = None

        dll_path = find_steam_dll(game_dir)
        if not dll_path:
            raise RuntimeError("steam_api64.dll not found (pass --game <install dir>)")
        self.log("steam dll: %s" % dll_path)
        self.dll = ctypes.WinDLL(dll_path)

        # already have the flat API? prove it rather than assuming (playbook lesson)
        for sym in ("SteamAPI_InitFlat", "SteamAPI_SteamUser_v023",
                    "SteamAPI_SteamMatchmaking_v009",
                    "SteamAPI_SteamNetworkingMessages_SteamAPI_v002",
                    "SteamAPI_ManualDispatch_Init", "SteamAPI_ManualDispatch_RunFrame"):
            if not hasattr(self.dll, sym):
                raise RuntimeError("steam dll is missing %s" % sym)

        self._declare()
        self._init()

    # -- prototypes -----------------------------------------------------
    def _declare(self):
        d = self.dll
        P, I, I32, I64, U32, U64, CSTR = (ctypes.c_void_p, ctypes.c_int,
                                          ctypes.c_int32, ctypes.c_int64,
                                          ctypes.c_uint32, ctypes.c_uint64, ctypes.c_char_p)

        d.SteamAPI_InitFlat.argtypes = [ctypes.c_char_p]
        d.SteamAPI_InitFlat.restype = I32

        d.SteamAPI_SteamUser_v023.restype = P
        d.SteamAPI_SteamMatchmaking_v009.restype = P
        d.SteamAPI_SteamNetworkingMessages_SteamAPI_v002.restype = P
        d.SteamAPI_SteamNetworkingUtils_SteamAPI_v004.restype = P

        d.SteamAPI_ISteamUser_GetSteamID.argtypes = [P]
        d.SteamAPI_ISteamUser_GetSteamID.restype = U64
        d.SteamAPI_ISteamUser_BLoggedOn.argtypes = [P]
        d.SteamAPI_ISteamUser_BLoggedOn.restype = ctypes.c_bool

        d.SteamAPI_ISteamMatchmaking_CreateLobby.argtypes = [P, I32, I32]
        d.SteamAPI_ISteamMatchmaking_CreateLobby.restype = U64
        d.SteamAPI_ISteamMatchmaking_JoinLobby.argtypes = [P, U64]
        d.SteamAPI_ISteamMatchmaking_JoinLobby.restype = U64
        d.SteamAPI_ISteamMatchmaking_LeaveLobby.argtypes = [P, U64]
        d.SteamAPI_ISteamMatchmaking_SetLobbyJoinable.argtypes = [P, U64, ctypes.c_bool]
        d.SteamAPI_ISteamMatchmaking_GetLobbyOwner.argtypes = [P, U64]
        d.SteamAPI_ISteamMatchmaking_GetLobbyOwner.restype = U64

        d.SteamAPI_GetHSteamPipe.restype = I32
        d.SteamAPI_ManualDispatch_Init.argtypes = []
        d.SteamAPI_ManualDispatch_RunFrame.argtypes = [I32]
        d.SteamAPI_ManualDispatch_GetNextCallback.argtypes = [I32, ctypes.POINTER(CallbackMsg)]
        d.SteamAPI_ManualDispatch_GetNextCallback.restype = ctypes.c_bool
        d.SteamAPI_ManualDispatch_FreeLastCallback.argtypes = [I32]
        d.SteamAPI_ManualDispatch_GetAPICallResult.argtypes = [
            I32, U64, ctypes.c_void_p, I32, I32, ctypes.POINTER(ctypes.c_bool)]
        d.SteamAPI_ManualDispatch_GetAPICallResult.restype = ctypes.c_bool

        d.SteamAPI_SteamNetworkingIdentity_SetSteamID64.argtypes = [P, U64]
        d.SteamAPI_SteamNetworkingIdentity_GetSteamID64.argtypes = [P]
        d.SteamAPI_SteamNetworkingIdentity_GetSteamID64.restype = U64

        d.SteamAPI_ISteamNetworkingMessages_SendMessageToUser.argtypes = [
            P, P, ctypes.c_void_p, U32, I32, I32]
        d.SteamAPI_ISteamNetworkingMessages_SendMessageToUser.restype = I32
        d.SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel.argtypes = [
            P, I32, ctypes.POINTER(P), I32]
        d.SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel.restype = I32
        d.SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser.argtypes = [P, P]
        d.SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser.restype = ctypes.c_bool

        d.SteamAPI_SteamNetworkingMessage_t_Release.argtypes = [P]

    # -- init -----------------------------------------------------------
    def _init(self):
        # Steam reads steam_appid.txt from the working directory
        with open("steam_appid.txt", "w") as f:
            f.write("480")

        err = ctypes.create_string_buffer(1024)
        res = self.dll.SteamAPI_InitFlat(err)
        if res != 0:
            raise RuntimeError("SteamAPI_InitFlat failed (%d): %s" % (res, err.value.decode("utf-8", "replace")))
        self.log("steam init ok")

        self.uiface = self.dll.SteamAPI_SteamUser_v023()
        self.matchmaking = self.dll.SteamAPI_SteamMatchmaking_v009()
        self.messages = self.dll.SteamAPI_SteamNetworkingMessages_SteamAPI_v002()

        if not self.dll.SteamAPI_ISteamUser_BLoggedOn(self.uiface):
            raise RuntimeError("Steam is running but not logged in")
        self.me = self.dll.SteamAPI_ISteamUser_GetSteamID(self.uiface)
        self.log("logged on as %d" % self.me)

        self.pipe = self.dll.SteamAPI_GetHSteamPipe()
        self.dll.SteamAPI_ManualDispatch_Init()
        self.log("manual dispatch ready (pipe %d)" % self.pipe)

    # -- callbacks ------------------------------------------------------
    def _pump(self, handler=None):
        self.dll.SteamAPI_ManualDispatch_RunFrame(self.pipe)
        msg = CallbackMsg()
        while self.dll.SteamAPI_ManualDispatch_GetNextCallback(self.pipe, ctypes.byref(msg)):
            cb = msg.callback
            if cb == CALLBACK_STEAM_API_CALL_COMPLETED:
                done = ctypes.cast(msg.param, ctypes.POINTER(SteamAPICallCompleted)).contents
                buf = ctypes.create_string_buffer(done.param_size)
                failed = ctypes.c_bool(False)
                ok = self.dll.SteamAPI_ManualDispatch_GetAPICallResult(
                    self.pipe, done.call, buf, done.param_size, done.callback, ctypes.byref(failed))
                if ok and not failed.value and done.callback == CALLBACK_LOBBY_CREATED:
                    lc = LobbyCreated.from_buffer_copy(buf.raw)
                    self.log("LobbyCreated result=%d lobby=%d" % (lc.result, lc.lobby))
                    self.lobby_id = lc.lobby
                elif ok and not failed.value and done.callback == CALLBACK_LOBBY_ENTER:
                    le = LobbyEnter.from_buffer_copy(buf.raw)
                    self.log("LobbyEnter lobby=%d response=%d" % (le.lobby, le.response))
                    self.lobby_id = le.lobby
            elif cb == CALLBACK_NET_SESSION_REQUEST:
                ident = ctypes.create_string_buffer(msg.param, msg.param_size)
                sid = self.dll.SteamAPI_SteamNetworkingIdentity_GetSteamID64(ctypes.byref(ident))
                self.log("session request from %d (accepting)" % sid)
                self.dll.SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(
                    self.messages, ctypes.byref(ident))
                self.peer_ids.add(sid)
            elif cb == CALLBACK_NET_SESSION_FAILED:
                self.log("session failed (a peer dropped)")
            elif cb == CALLBACK_LOBBY_ENTER:
                le = ctypes.cast(msg.param, ctypes.POINTER(LobbyEnter)).contents
                self.log("LobbyEnter lobby=%d response=%d" % (le.lobby, le.response))
                self.lobby_id = le.lobby
            else:
                self.log("callback %d" % cb)
            self.dll.SteamAPI_ManualDispatch_FreeLastCallback(self.pipe)
            if handler:
                handler(cb)

    # -- lobby ----------------------------------------------------------
    def establish(self):
        if self.role == "host":
            self.log("creating friends-only lobby")
            self.dll.SteamAPI_ISteamMatchmaking_CreateLobby(self.matchmaking, 1, 2)
        elif self.lobby_id:
            self.log("joining lobby %s" % self.lobby_id)
            self.dll.SteamAPI_ISteamMatchmaking_JoinLobby(self.matchmaking, int(self.lobby_id))
        else:
            raise RuntimeError("client needs --lobby <id>")

        deadline = time.time() + 30
        while time.time() < deadline:
            self._pump()
            if self.lobby_id:
                break
            time.sleep(0.05)

        if not self.lobby_id:
            raise RuntimeError("no lobby after 30 s")
        self.dll.SteamAPI_ISteamMatchmaking_SetLobbyJoinable(self.matchmaking, self.lobby_id, True)
        self.log("LOBBY=%d  (friend joins with --lobby %d)" % (self.lobby_id, self.lobby_id))

        # host waits for the friend's first message; client waits for the host's
        if self.role == "host":
            self.host_id = self.me
        else:
            self.host_id = self.dll.SteamAPI_ISteamMatchmaking_GetLobbyOwner(self.matchmaking, self.lobby_id)
            self.log("host is %d" % (self.host_id or 0))
        return self.lobby_id

    # -- data -----------------------------------------------------------
    def _identity(self, steam_id):
        buf = ctypes.create_string_buffer(IDENTITY_SIZE)
        self.dll.SteamAPI_SteamNetworkingIdentity_SetSteamID64(ctypes.byref(buf), steam_id)
        return buf

    def send(self, text, reliable=False):
        targets = list(self.peer_ids)
        if not targets and self.host_id and self.host_id != self.me:
            targets = [self.host_id]
        data = text.encode("utf-8")
        buf = ctypes.create_string_buffer(data, len(data))
        for t in targets:
            ident = self._identity(t)
            flags = (SEND_RELIABLE if reliable else SEND_UNRELIABLE) | SEND_NONAGLE
            self.dll.SteamAPI_ISteamNetworkingMessages_SendMessageToUser(
                self.messages, ctypes.byref(ident), buf, len(data), flags, CHANNEL)

    def receive(self):
        arr = (ctypes.c_void_p * MAX_MESSAGES)()
        out = []
        n = self.dll.SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(
            self.messages, CHANNEL, arr, MAX_MESSAGES)
        for i in range(max(0, min(n, MAX_MESSAGES))):
            ptr = arr[i]
            if not ptr:
                continue
            size = ctypes.c_uint32.from_address(ptr + 8).value
            data = ctypes.string_at(ctypes.c_void_p.from_address(ptr).value or 0, size)
            try:
                out.append(data.decode("utf-8", "replace").strip())
            except Exception:
                pass
            self.dll.SteamAPI_SteamNetworkingMessage_t_Release(ptr)
        return out

    def close(self):
        try:
            if self.lobby_id:
                self.dll.SteamAPI_ISteamMatchmaking_LeaveLobby(self.matchmaking, self.lobby_id)
        except Exception:
            pass
