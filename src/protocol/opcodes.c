#include "openbc/opcodes.h"
#include <stddef.h>

const char *bc_opcode_name(int opcode)
{
    switch (opcode) {
    /* Game opcodes */
    case BC_OP_SETTINGS:           return "Settings";
    case BC_OP_GAME_INIT:          return "GameInit";
    case BC_OP_OBJ_CREATE:         return "ObjCreate";
    case BC_OP_OBJ_CREATE_TEAM:    return "ObjCreateTeam";
    case BC_OP_BOOT_PLAYER:        return "BootPlayer";
    case BC_OP_PYTHON_EVENT:       return "PythonEvent";
    case BC_OP_START_FIRING:       return "StartFiring";
    case BC_OP_STOP_FIRING:        return "StopFiring";
    case BC_OP_STOP_FIRING_AT:     return "StopFiringAt";
    case BC_OP_SUBSYS_STATUS:      return "SubsysStatus";
    case BC_OP_EVENT_FWD_DF:       return "EventFwd_DF";
    case BC_OP_EVENT_FWD:          return "EventFwd";
    case BC_OP_PYTHON_EVENT2:      return "PythonEvent2";
    case BC_OP_START_CLOAK:        return "StartCloak";
    case BC_OP_STOP_CLOAK:         return "StopCloak";
    case BC_OP_START_WARP:         return "StartWarp";
    case BC_OP_HOST_MSG:           return "HostMsg";
    case BC_OP_DESTROY_OBJ:        return "DestroyObj";
    case BC_OP_UNKNOWN_15:         return "Unknown_15";
    case BC_OP_UI_SETTINGS:        return "UISettings";
    case BC_OP_DELETE_PLAYER_UI:   return "DeletePlayerUI";
    case BC_OP_DELETE_PLAYER_ANIM: return "DeletePlayerAnim";
    case BC_OP_TORPEDO_FIRE:       return "TorpedoFire";
    case BC_OP_BEAM_FIRE:          return "BeamFire";
    case BC_OP_TORP_TYPE_CHANGE:   return "TorpTypeChange";
    case BC_OP_STATE_UPDATE:       return "StateUpdate";
    case BC_OP_OBJ_NOT_FOUND:      return "ObjNotFound";
    case BC_OP_REQUEST_OBJ:        return "RequestObj";
    case BC_OP_ENTER_SET:          return "EnterSet";
    case BC_OP_EXPLOSION:          return "Explosion";
    case BC_OP_NEW_PLAYER_IN_GAME: return "NewPlayerInGame";
    /* NetFile / Checksum opcodes */
    case BC_OP_CHECKSUM_REQ:       return "ChecksumReq";
    case BC_OP_CHECKSUM_RESP:      return "ChecksumResp";
    case BC_OP_VERSION_MISMATCH:   return "VersionMismatch";
    case BC_OP_SYS_CHECKSUM_FAIL:  return "SysChecksumFail";
    case BC_OP_FILE_TRANSFER:      return "FileTransfer";
    case BC_OP_FILE_TRANSFER_ACK:  return "FileTransferACK";
    case BC_OP_UNKNOWN_28:         return "Unknown_28";
    /* Python messages */
    case BC_MSG_CHAT:              return "ChatMessage";
    case BC_MSG_TEAM_CHAT:         return "TeamChatMessage";
    case BC_MSG_MISSION_INIT:      return "MissionInit";
    case BC_MSG_SCORE_CHANGE:      return "ScoreChange";
    case BC_MSG_SCORE:             return "Score";
    case BC_MSG_END_GAME:          return "EndGame";
    case BC_MSG_RESTART:           return "RestartGame";
    case BC_MSG_SCORE_INIT:        return "ScoreInit";
    case BC_MSG_TEAM_SCORE:        return "TeamScore";
    case BC_MSG_TEAM_MESSAGE:      return "TeamMessage";
    default:                       return NULL;
    }
}
