# -*- coding: utf-8 -*-
from .IPC.McTools import FIND_BEH_FILE
from .IPC.qpyipc import PyIPC
lambda: "By Zero123 仅供参考，请勿用于非法用途"

class ChronoManager:
    _INSTANCE = None

    @staticmethod
    def getInstance():
        if ChronoManager._INSTANCE is None:
            ChronoManager._INSTANCE = ChronoManager()
        return ChronoManager._INSTANCE

    def __init__(self):
        self.mSpeed = 1.0
        self.ipcState = False
        self.mIpc = PyIPC(FIND_BEH_FILE("bins/chronoApi.exe"))

    def startIPC(self):
        if self.ipcState:
            return False
        self.ipcState = True
        return self.mIpc.start()

    def setSpeed(self, speed):
        # type: (float) -> bool
        """ 同步设置游戏速度 """
        if speed < 0.0:
            return False
        if self.mSpeed == speed:
            return False
        self.mSpeed = speed
        self.startIPC()
        self.mIpc.get("set_game_speed", {"value": float(speed)}, timeout=5.0)
        return True

    def asyncSetSpeed(self, speed):
        """ 异步设置游戏速度 """
        # type: (float) -> bool
        if speed < 0.0:
            return False
        if self.mSpeed == speed:
            return False
        self.mSpeed = speed
        self.startIPC()
        self.mIpc.request("set_game_speed", {"value": float(speed)})
        return True

    def closeHook(self):
        """ 安全关闭 """
        if not self.ipcState:
            return False
        self.ipcState = False
        # self.mIpc.get("safe_close", timeout=5.0)
        if self.mIpc.isProcAlive():
            # print("安全关闭时间速率MOD进程...")
            # self.mIpc.get("safe_close", timeout=5.0) # 由CPP自己安全关闭进程
            self.mIpc.get("set_game_speed", {"value": 1.0}, timeout=5.0)
            self.mIpc.stop()
        return True