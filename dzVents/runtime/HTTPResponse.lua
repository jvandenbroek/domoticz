local _  = require('lodash')
local utils = require('Utils')
local evenItemIdentifier = require('eventItemIdentifier')

local function HTTPResponce(domoticz, responseData)

    local self = {}

    self.headers = responseData.headers or {}

    self.data = responseData.data or nil

    self._contentType = _.get(self.headers, {'Content-Type'}, '')

    self.isJSON = false

    self.statusCode = responseData.statusCode

    self.ok = false
    if (self.statusCode >= 200 and self.statusCode <= 299) then
        self.ok = true
    end

    self.callback = responseData.callback

    evenItemIdentifier.setType(self, 'isHTTPResponse', domoticz.BASETYPE_HTTP_RESPONSE, responseData.callback)

    if (string.match(self._contentType, 'application/json') and self.data) then
        local json = utils.fromJSON(self.data)

        if (json) then
            self.isJSON = true
            self.json = json
        end
    end

    return self
end

return HTTPResponce
