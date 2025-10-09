# -*- coding: utf-8 -*-
from .QuModLibs.QuMod import *
from .ChronoTool import ChronoManager

chronoMod = EasyMod()

HOOK_PY_TIMR = True

instance = ChronoManager.getInstance()
if HOOK_PY_TIMR:
    instance.hookPyTime()

SYSTEM_REF = 0

# 注册任意端侧析构重置逻辑
@REG_SERVER_INIT_CALL
def initServer():
    from .QuModLibs.Server import DestroyFunc
    global SYSTEM_REF
    @DestroyFunc
    def onDestroy():
        instance.closeHook()
        global SYSTEM_REF
        SYSTEM_REF -= 1
        if HOOK_PY_TIMR and SYSTEM_REF <= 0:
            instance.restHookPyTime()
    instance.startIPC()
    SYSTEM_REF += 1

@REG_CLIENT_INIT_CALL
def initClient():
    from .QuModLibs.Client import DestroyFunc
    global SYSTEM_REF
    @DestroyFunc
    def onDestroy():
        instance.closeHook()
        global SYSTEM_REF
        SYSTEM_REF -= 1
        if HOOK_PY_TIMR and SYSTEM_REF <= 0:
            instance.restHookPyTime()
    instance.startIPC()
    SYSTEM_REF += 1

# 注册端侧逻辑模块
chronoMod.Server("Server") \
    .Client("Client")