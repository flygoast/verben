--
-- Lua echo demo for verben lua plugin.
--

local _v = verben
verben = nil
local verben = _v
_v = nil

log     = verben.log
config  = verben.config

function handle_init(conf, t)
    if (t == 0) then
        log("DEBUG", "lua handle_init in MASTER")
    elseif (t == 1) then
        log("DEBUG", "lua handle_init in WORKER")
    elseif (t == 2) then
        log("DEBUG", "lua handle_init in CONN")
    end
    return 0
end

function handle_open(params)
    log("DEBUG", "Connection from ", params["remote_ip"], ":",
        params["remote_port"])
    return 0, "Welcome to verben lua\r\n"
end

function handle_close(params)
    log("DEBUG", "Connection from ", params["remote_ip"], ":",
        params["remote_port"], " closed")
end

function handle_input(params)
    log("DEBUG", "connection:", params["remote_ip"], ":", 
        params["remote_port"]);

    return params["length"]
end

function handle_process(params)
    log("DEBUG", "Connection from ", params["remote_ip"], ":",
        params["remote_port"], " closed")
    return 0, params["content"];
end

function handle_fini(conf, t)
    if (t == 0) then
        log("DEBUG", "lua handle_fini in MASTER")
    elseif (t == 1) then
        log("DEBUG", "lua handle_fini in WORKER")
    elseif (t == 2) then
        log("DEBUG", "lua handle_fini in CONN")
    end
end
