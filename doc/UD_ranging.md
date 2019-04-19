normal mode

```plantuml
@startuml
autonumber
activate Anchor1
Anchor1 -[#0000FF]->TAG : Rang init
TAG -[#red]> Anchor1 : Poll
Anchor1 -[#0000FF]->TAG : Poll Ack
TAG -[#red]> Anchor1 : Range
deactivate Anchor1
Anchor1 -[#005500]->Anchor1 : Sleep
autonumber 1
activate Anchor2
Anchor2 -[#0000FF]->TAG : Rang init
TAG -[#red]> Anchor2 : Poll
Anchor2 -[#0000FF]->TAG : Poll Ack
TAG -[#red]> Anchor2 : Range
deactivate Anchor2
Anchor2 -[#005500]->Anchor2 : Sleep
@enduml
```

pair mode

```plantuml
@startuml
group boardcast
TAG -[#red]> Anchor1 : Blink
TAG -[#red]> Anchor2 : Blink
end
Anchor1 -[#0000FF]->TAG : Anchor(#)
TAG-[#005500]->TAG : Add Address
Anchor2 -[#0000FF]->TAG : Anchor(#)
TAG-[#005500]->TAG : Add Address
@enduml
```

status machine Tag

```plantuml
@startuml
hide empty description
[*] --[#violet]> IdleMode : Powerup
IdleMode --[#blue]>  PairMode : Pair Mode on
state PairMode{
    [*] --[#blue]> Send_BLINK
    Send_BLINK --[#blue]> Add_Anchor : Got **ACK**
    Add_Anchor --[#blue]> Send_BLINK : **ACK** timout
    Add_Anchor --[#blue]> [*]  : Added 2 Anchor
}
PairMode --[#blue]> IdleMode : Done
IdleMode --[#red]>  RangeMode : RangeMode
state RangeMode{
    [*] --[#red]> Send_POLL
    Send_POLL --[#red]> Send_RANGE : Got **POLL_ACK**
    Send_RANGE --[#red]> [*]  : Sended
}
RangeMode --[#red]> IdleMode : Done
@enduml
```

status machine Anchor

```plantuml
@startuml
hide empty description
[*] --[#violet]> IdleMode : Powerup
IdleMode --[#blue]>  Send_ACK : Got **BLINK**
Send_ACK --[#blue]> IdleMode : Done
IdleMode --[#red]>  RangeMode : Got **POLL**
state RangeMode{
    [*] --[#red]> Send_POLL_ACK
    Send_POLL_ACK --[#red]> Wait_RANGE
    Wait_RANGE --[#red]> Computate  : Got **RANGE**
    Wait_RANGE --[#violet]> [*] : Timeout
    Computate --[#red]> [*]  : Done
}
RangeMode --[#red]> IdleMode : Done
@enduml
```

TODO: We need to write the 12 octets for Minimum IEEE ID blink for DWM1000 MAC
**BLINK 都是自訂格式**
DWM1000 官方給的格式跟Arduino Library 都是自訂的

那我們自己可以為了不要撞在一起就用Frame Filter
但是就要符合 IEEE 802.15.4-2011 規定

BLINK package
```ditaa {cmd=true args=["-E"]}

+--------+----------+----------+----------+
| Frame  | Sequence |  TAG ID  |    FCS   |
| Control| Number   |          |          |
+--------+----------+----------+----------+
|  0xC5  | 1 octet  | 8 octets | 2 octets |
+--------+----------+----------+----------+
```

先確認Frame Filter ok
那就可以開始準備使用Frame Filter 進行流程管理

可以參考
int testapprun(instance_data_t *inst, int message, uint32 time_ms)
EVK1000