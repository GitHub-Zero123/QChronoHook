# -*- coding: utf-8 -*-
from .QuModLibs.Server import *
from .ChronoTool import ChronoManager

@Listen("ServerItemTryUseEvent")
def ServerItemTryUseEvent(args={}):
    itemDict = args["itemDict"]
    if itemDict and itemDict["newItemName"] == "minecraft:clock":
        # 时间速率数据以服务端为准
        Call(
            args["playerId"],
            "OpenClockScreen",
            ChronoManager.getInstance().mSpeed
        )

@AllowCall
def UPDATE_GAME_SPEED(speed):
    ChronoManager.getInstance().asyncSetSpeed(speed)