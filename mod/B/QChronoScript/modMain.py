# -*- coding: utf-8 -*-
from .QuModLibs.QuMod import *
from .ChronoTool import ChronoManager

chronoMod = EasyMod()

# 注册任意端侧析构重置逻辑
@REG_SERVER_INIT_CALL
def initServer():
    from .QuModLibs.Server import DestroyFunc
    insatnce = ChronoManager.getInstance()
    @DestroyFunc
    def onDestroy():
        insatnce.closeHook()
    insatnce.startIPC()

@REG_CLIENT_INIT_CALL
def initClient():
    from .QuModLibs.Client import DestroyFunc
    insatnce = ChronoManager.getInstance()
    @DestroyFunc
    def onDestroy():
        insatnce.closeHook()
    insatnce.startIPC()

# 注册端侧逻辑模块
chronoMod.Server("Server") \
    .Client("Client")