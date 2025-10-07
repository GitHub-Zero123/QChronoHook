# -*- coding: utf-8 -*-
from .QuModLibs.Client import *
from .QuModLibs.UI import ScreenNodeWrapper

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

@ScreenNodeWrapper.autoRegister("QChronoEditer.main")
class ChronoEditUI(ScreenNodeWrapper):
    def __init__(self, namespace, name, param):
        ScreenNodeWrapper.__init__(self, namespace, name, param)
        self.mSpeed = Static._SYNC_SPEED    # 拉取数据
        self.uiLoading = False
        self.maxValue = 2.5
        self.cSpeed = -1.0
    
    def Create(self):
        ScreenNodeWrapper.Create(self)
        self.uiLoading = True
        @self.bindButtonClickHandler("/background/close")
        def onCloseBtnClick():
            newSpeed = self.getSliderValue()
            if newSpeed != self.mSpeed:
                UPDATE_GAME_SPEED(self.getSliderValue())
            return self.SetRemove()
        self.getSlider().SetSliderValue(self.mSpeed / self.maxValue)
    
    def getSlider(self):
        return self.GetBaseUIControl("/background/slider").asSlider()

    def getSliderValue(self):
        return round(self.getSlider().GetSliderValue() * self.maxValue, 2)

    def Update(self):
        ScreenNodeWrapper.Update(self)
        if not self.uiLoading:
            return
        newSpeed = self.getSliderValue()
        if newSpeed == self.cSpeed:
            # 重复检查
            return
        self.cSpeed = newSpeed
        self.GetBaseUIControl("/background/render").asLabel().SetText("当前速率: {}x".format(newSpeed))

    def Destroy(self):
        ScreenNodeWrapper.Destroy(self)
        self.uiLoading = False

@AllowCall
def OpenClockScreen(speed):
    # 同步服务端的新时间速度数据
    Static._SYNC_SPEED = speed
    ChronoEditUI.pushScreen()

def UPDATE_GAME_SPEED(speed):
    # type: (float) -> None
    return Call("UPDATE_GAME_SPEED", float(speed))