local _  = require('lodash')
local utils = require('Utils')
local evenItemIdentifier = require('eventItemIdentifier')

local function DomoticzEvent(domoticz, eventData)

    local self = {}

    self.type = eventData.type
    self.status = eventData.status
    self.message = eventData.message

    evenItemIdentifier.setType(self, 'isDomoticzEvent', domoticz.BASE_TYPE_DOMOTICZ_EVENT, eventData.type)

    return self
end

return DomoticzEvent
