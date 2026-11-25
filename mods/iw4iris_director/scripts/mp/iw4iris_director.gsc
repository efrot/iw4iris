/*
    IW4Iris Director Mod - entry point
    Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.

    IW4x loads every script in scripts/mp/ when a multiplayer map starts and
    calls its init(). The Director Mod stays inactive unless the IW4Iris
    editor started this map as an offline film set: IW4IrisDirectorActive()
    is a builtin of the IW4Iris client and answers false otherwise.
*/

init()
{
    if ( !IW4IrisDirectorActive() )
        return;

    scripts\iw4iris\director::init();
}
