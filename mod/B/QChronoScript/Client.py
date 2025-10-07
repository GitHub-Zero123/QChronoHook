# -*- coding: utf-8 -*-
from .QuModLibs.Client import *

class Static:
    _UI_INITED = False
    _SYNC_SPEED = 1.0

@Listen("UiInitFinished")
def UiInitFinished(_={}):
    if Static._UI_INITED:
        return
    Static._UI_INITED = True
    comp = clientApi.GetEngineCompFactory().CreateTextNotifyClient(levelId)
    comp.SetLeftCornerNotify("时间变速MOD已加载，使用原版时钟打开编辑界面")

@AllowCall
def OpenClockScreen(speed):
    # 同步服务端的新时间速度数据
    Static._SYNC_SPEED = speed