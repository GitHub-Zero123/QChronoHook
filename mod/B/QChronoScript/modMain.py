# -*- coding: utf-8 -*-
from .QuModLibs.QuMod import *
from .ChronoTool import ChronoManager

chronoMod = EasyMod()

# 注册任意端侧析构重置逻辑
@REG_SERVER_INIT_CALL
def initServer():
    from .QuModLibs.Server import regModLoadFinishHandler
    @regModLoadFinishHandler
    def destroy():
        ChronoManager.getInstance().closeHook()

@REG_CLIENT_MODULE
def initClient():
    from .QuModLibs.Client import regModLoadFinishHandler
    @regModLoadFinishHandler
    def destroy():
        ChronoManager.getInstance().closeHook()