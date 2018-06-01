local _  = require('lodash')
local utils = require('Utils')
local evenItemIdentifier = require('eventItemIdentifier')

local function SystemEvent(domoticz, eventData)

    local self = {}

    self.type = eventData.type
    self.status = eventData.status
    self.message = eventData.message

    evenItemIdentifier.setType(self, 'isSystem', domoticz.BASE_TYPE_SYSTEM, eventData.type)

    return self
end

return SystemEvent
