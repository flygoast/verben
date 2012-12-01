--
-- Lua echo demo for verben lua plugin.
--

local _v = verben
verben = nil
local verben = _v
_v = nil

log = verben.log

function handle_init()
    log("DEBUG", "handle_init in lua")
end

