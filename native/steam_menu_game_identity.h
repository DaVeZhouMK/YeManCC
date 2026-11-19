#pragma once
#include "steam_game_process_facts.h"

// PRIVATE UNBUILT CONSUMER. Process identity and held host facts come only
// from the public Steam owner. This header produces no OS or tracker facts.
namespace ymcc::menugame {
using Process = ymcc::steamgamefacts::ProcessWitness;
using ymcc::steamgamefacts::exactGameId;
using ymcc::steamgamefacts::samePath;
}
