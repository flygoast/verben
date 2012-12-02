--
-- Lua echo demo for verben lua plugin.
--

local _v = verben
verben = nil
local verben = _v
_v = nil

-- functions exported from vlua core.
log               = verben.log
config            = verben.config

-- contants exported from vlua core.
MASTER            = verben.MASTER
WORKER            = verben.WORKER
CONN              = verben.CONN
FATAL             = verben.FATAL
ERROR             = verben.ERROR
WRANING           = verben.WARNING
NOTICE            = verben.NOTICE
DEBUG             = verben.DEBUG
VERBEN_OK         = verben.VERBEN_OK
VERBEN_ERROR      = verben.VERBEN_ERROR
VERBEN_CONN_CLOSE = verben.VERBEN_CONN_CLOSE

function handle_init(conf, t)
    if t == MASTER then
        log(DEBUG, "lua handle_init in MASTER")
    elseif t == WORKER then
        log(DEBUG, "lua handle_init in WORKER")
    elseif t == CONN then
        log(DEBUG, "lua handle_init in CONN")
    end
    return VERBEN_OK
end

function handle_open(params)
    log(DEBUG, "Connection from ", params["remote_ip"], ":",
        params["remote_port"])
    time = os.time()

    return VERBEN_OK, "Welcome to verben lua [" .. time .. "]\r\n"
end

function handle_close(params)
    log(DEBUG, "Connection from ", params["remote_ip"], ":",
        params["remote_port"], " closed")
end

function handle_input(params)
    log(DEBUG, "Connection:", params["remote_ip"], ":", 
        params["remote_port"], "length: ", params["length"])

    return params["length"]
end

function handle_process(params)
    log(DEBUG, params["content"], ":", params["length"])
    return VERBEN_OK, params["content"]
end

function handle_fini(conf, t)
    if t == MASTER then
        log(DEBUG, "lua handle_fini in MASTER")
    elseif t == WORKER then
        log(DEBUG, "lua handle_fini in WORKER")
    elseif t == CONN then
        log(DEBUG, "lua handle_fini in CONN")
    end
end
