# -*- coding: utf-8 -*-
from .IPC.McTools import FIND_BEH_FILE
from .IPC.qpyipc import PyIPC
import time as pytime
from time import time
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
        # 虚拟时间系统初始化
        self._realStart = time()                # 记录真实时间起点
        self._virtualStart = self._realStart    # 记录虚拟时间起点

    def _updateVirtualTime(self):
        """ 更新虚拟时间 """
        nowReal = time()
        self._virtualStart = self.mTimeRaw(nowReal)
        self._realStart = nowReal

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
        self._updateVirtualTime()
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
        self._updateVirtualTime()
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
        self._updateVirtualTime()
        self.mSpeed = 1.0
        return True

    def mTimeRaw(self, nowReal=None):
        """根据真实时间计算虚拟时间"""
        if nowReal is None:
            nowReal = time()
        return self._virtualStart + (nowReal - self._realStart) * self.mSpeed

    def mTime(self):
        """替代 time.time 的函数"""
        return self.mTimeRaw()

    def hookPyTime(self):
        if pytime.time == self.mTime:
            return False
        pytime.time = self.mTime
        return True

    def restHookPyTime(self):
        if pytime.time == self.mTime:
            pytime.time = time
            return True
        return False