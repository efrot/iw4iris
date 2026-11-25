/*
    IW4Iris Director Mod — film set runtime
    Copyright (C) 2026 IW4Iris contributors. Licensed under GPL-3.0-or-later.

    The native editor owns the scene; this script owns the game entities.
    Every server frame it
      1. executes the editor's requests (spawn / change / delete objects,
         play animations) and answers each one,
      2. applies the transforms and visibility the editor evaluated for the
         current timeline time,
      3. plays the effects that are due,
      4. reports the bone positions the editor asked for (graphics that
         track an actor's head, hands, ...).

    Requests and transforms come from IW4Iris builtins (protocol 2, see
    docs/DIRECTOR_PROTOCOL.md). Only assets the editor validated and listed
    in the manifest are used, and the manifest is precached in init() as
    the engine requires, so a missing asset can never reach setModel().

    Actor technique (as used by MW2 cinematic mods): a script_model body
    playing multiplayer player animations, a separate head model linked to
    the body's j_spine4 bone and playing the same animation, and weapon /
    equipment models linked to bone tags.
*/

init()
{
    level.iw4iris = spawnStruct();
    level.iw4iris.protocol = 2;
    level.iw4iris.objects = [];
    level.iw4iris.fx = [];

    precacheManifest();

    level thread onPlayerConnect();
    level thread frameLoop();
}

precacheManifest()
{
    // Precaching has to happen before the first wait of the level.
    manifest = IW4IrisManifest();
    models = 0;
    anims = 0;
    weapons = 0;
    effects = 0;
    for ( i = 0; i < manifest.size; i++ )
    {
        parts = strTok( manifest[i], ":" );
        if ( parts.size < 2 )
            continue;

        kind = parts[0];
        name = parts[1];
        if ( kind == "model" )
        {
            precacheModel( name );
            models++;
        }
        else if ( kind == "anim" )
        {
            precacheMPAnim( name );
            anims++;
        }
        else if ( kind == "weapon" )
        {
            precacheItem( name );
            weapons++;
        }
        else if ( kind == "fx" )
        {
            level.iw4iris.fx[name] = loadFX( name );
            effects++;
        }
    }
    IW4IrisLog( "precached " + models + " models, " + anims + " animations, " + weapons + " weapons, " + effects + " effects" );
}

// ----------------------------------------------------------------- players

onPlayerConnect()
{
    for ( ;; )
    {
        level waittill( "connected", player );
        player thread becomeSpectator();
    }
}

// The local player only operates the camera: it joins as a spectator so
// it has no body in the shots and the match never starts around it.
becomeSpectator()
{
    self endon( "disconnect" );

    wait 0.05;
    if ( isDefined( game["menu_team"] ) )
        self notify( "menuresponse", game["menu_team"], "spectator" );
    wait 0.05;
    self closepopupMenu();
    self closeInGameMenu();
}

// ----------------------------------------------------------------- frame loop

frameLoop()
{
    level endon( "game_ended" );

    IW4IrisReady( "protocol " + level.iw4iris.protocol + " actors props effects tags" );
    for ( ;; )
    {
        // A bounded number per frame keeps a large scene load from stalling
        // the server frame; the rest follow next frame.
        // Every request and step runs in its own thread: a script error
        // ends only that thread (the editor then sees the request time out
        // and retries or reports it), never this loop.
        for ( n = 0; n < 48; n++ )
        {
            request = IW4IrisNextRequest();
            if ( request.size == 0 )
                break;
            level thread handleRequest( request );
        }

        level thread applyPoses();
        level thread playEffects();
        level thread reportTags();
        wait 0.05;
    }
}

// Every request is [op, request id, arguments...] and is answered exactly
// once, "ok" only after the operation was carried out.
handleRequest( r )
{
    if ( r.size < 2 )
        return;

    op = r[0];
    id = r[1];
    switch ( op )
    {
        case "spawnActor":
            if ( hasArgs( r, 5 ) )
                spawnActor( id, r[2], r[3], r[4] );
            break;
        case "setWeapon":
            if ( hasArgs( r, 6 ) )
                setWeapon( id, r[2], r[3], r[4], int( r[5] ) );
            break;
        case "setAttachments":
            if ( hasArgs( r, 4 ) && hasArgs( r, 4 + int( r[3] ) * 4 ) )
                setAttachments( id, r );
            break;
        case "anim":
            if ( hasArgs( r, 4 ) )
                playAnimation( id, r[2], r[3] );
            break;
        case "spawnProp":
            if ( hasArgs( r, 4 ) )
                spawnProp( id, r[2], r[3] );
            break;
        case "linkProp":
            if ( hasArgs( r, 5 ) )
                linkProp( id, r[2], r[3], r[4] );
            break;
        case "unlinkProp":
            if ( hasArgs( r, 3 ) )
                unlinkProp( id, r[2] );
            break;
        case "delete":
            if ( hasArgs( r, 3 ) )
                deleteObject( id, r[2] );
            break;
        case "clear":
            clearAll( id );
            break;
        case "ping":
            IW4IrisRespond( id, "ok", "protocol " + level.iw4iris.protocol );
            break;
        default:
            IW4IrisRespond( id, "error", "unknown operation " + op );
            break;
    }
}

hasArgs( r, count )
{
    if ( r.size >= count )
        return true;
    IW4IrisRespond( r[1], "error", r[0] + " needs " + count + " fields, got " + r.size );
    return false;
}

objectOrFail( requestId, objectId )
{
    obj = level.iw4iris.objects[objectId];
    if ( !isDefined( obj ) )
        IW4IrisRespond( requestId, "error", "no object " + objectId );
    return obj;
}

// ----------------------------------------------------------------- actors

spawnActor( requestId, objectId, body, head )
{
    if ( isDefined( level.iw4iris.objects[objectId] ) )
        deleteParts( level.iw4iris.objects[objectId] );

    obj = spawnStruct();
    obj.kind = "actor";
    obj.visible = 1;
    obj.linked = false;
    obj.attachments = [];

    // Spawned far below the map; the first pose moves it into place.
    obj.body = spawn( "script_model", ( 0, 0, -20000 ) );
    obj.body setModel( body );
    obj.body.angles = ( 0, 0, 0 );

    if ( head != "" )
    {
        obj.head = spawn( "script_model", obj.body getTagOrigin( "j_spine4" ) );
        obj.head setModel( head );
        obj.head.angles = obj.body.angles + ( 270, 0, 270 );
        obj.head enableLinkTo();
        obj.head linkTo( obj.body, "j_spine4" );
    }

    level.iw4iris.objects[objectId] = obj;
    IW4IrisRegister( objectId, "body", obj.body getEntityNumber() );
    IW4IrisRespond( requestId, "ok", "" );
}

playAnimation( requestId, objectId, anim )
{
    obj = objectOrFail( requestId, objectId );
    if ( !isDefined( obj ) )
        return;

    obj.body scriptModelPlayAnim( anim );
    if ( isDefined( obj.head ) )
        obj.head scriptModelPlayAnim( anim );
    IW4IrisRespond( requestId, "ok", "" );
}

setWeapon( requestId, objectId, weapon, tag, camo )
{
    obj = objectOrFail( requestId, objectId );
    if ( !isDefined( obj ) )
        return;

    if ( isDefined( obj.weapon ) )
    {
        obj.weapon unlink();
        obj.weapon delete();
        obj.weapon = undefined;
    }
    if ( weapon != "" )
    {
        model = getWeaponModel( weapon, camo );
        obj.weapon = spawnOnTag( obj.body, model, tag );
        hidden = getWeaponHideTags( weapon );
        for ( i = 0; i < hidden.size; i++ )
            obj.weapon hidePart( hidden[i], model );
        if ( !obj.visible )
            obj.weapon hide();
        IW4IrisRegister( objectId, "weapon", obj.weapon getEntityNumber() );
    }
    IW4IrisRespond( requestId, "ok", "" );
}

// [op, id, object, count, (kind, name, tag, camo) x count]: replaces every
// extra model of the actor in one step.
setAttachments( requestId, r )
{
    obj = objectOrFail( requestId, r[2] );
    if ( !isDefined( obj ) )
        return;

    for ( i = 0; i < obj.attachments.size; i++ )
    {
        obj.attachments[i] unlink();
        obj.attachments[i] delete();
    }
    obj.attachments = [];

    count = int( r[3] );
    for ( k = 0; k < count; k++ )
    {
        base = 4 + k * 4;
        kind = r[base];
        name = r[base + 1];
        tag = r[base + 2];
        camo = int( r[base + 3] );

        model = name;
        if ( kind == "weapon" )
            model = getWeaponModel( name, camo );
        part = spawnOnTag( obj.body, model, tag );
        if ( kind == "weapon" )
        {
            hidden = getWeaponHideTags( name );
            for ( i = 0; i < hidden.size; i++ )
                part hidePart( hidden[i], model );
        }
        if ( !obj.visible )
            part hide();
        obj.attachments[obj.attachments.size] = part;
    }
    IW4IrisRespond( requestId, "ok", "" );
}

spawnOnTag( parent, model, tag )
{
    part = spawn( "script_model", parent getTagOrigin( tag ) );
    part setModel( model );
    part.angles = parent getTagAngles( tag );
    part enableLinkTo();
    part linkTo( parent, tag, ( 0, 0, 0 ), ( 0, 0, 0 ) );
    return part;
}

// ----------------------------------------------------------------- props

spawnProp( requestId, objectId, model )
{
    if ( isDefined( level.iw4iris.objects[objectId] ) )
        deleteParts( level.iw4iris.objects[objectId] );

    obj = spawnStruct();
    obj.kind = "prop";
    obj.visible = 1;
    obj.linked = false;
    obj.attachments = [];
    obj.body = spawn( "script_model", ( 0, 0, -20000 ) );
    obj.body setModel( model );
    obj.body.angles = ( 0, 0, 0 );
    obj.body enableLinkTo();

    level.iw4iris.objects[objectId] = obj;
    IW4IrisRegister( objectId, "body", obj.body getEntityNumber() );
    IW4IrisRespond( requestId, "ok", "" );
}

linkProp( requestId, objectId, actorId, tag )
{
    obj = objectOrFail( requestId, objectId );
    if ( !isDefined( obj ) )
        return;
    actor = level.iw4iris.objects[actorId];
    if ( !isDefined( actor ) )
    {
        IW4IrisRespond( requestId, "error", "no actor " + actorId );
        return;
    }

    obj.body unlink();
    obj.body.origin = actor.body getTagOrigin( tag );
    obj.body.angles = actor.body getTagAngles( tag );
    obj.body linkTo( actor.body, tag, ( 0, 0, 0 ), ( 0, 0, 0 ) );
    obj.linked = true;
    IW4IrisRespond( requestId, "ok", "" );
}

unlinkProp( requestId, objectId )
{
    obj = objectOrFail( requestId, objectId );
    if ( !isDefined( obj ) )
        return;

    obj.body unlink();
    obj.linked = false;
    IW4IrisRespond( requestId, "ok", "" );
}

// ----------------------------------------------------------------- lifecycle

deleteParts( obj )
{
    for ( i = 0; i < obj.attachments.size; i++ )
    {
        obj.attachments[i] unlink();
        obj.attachments[i] delete();
    }
    if ( isDefined( obj.weapon ) )
    {
        obj.weapon unlink();
        obj.weapon delete();
    }
    if ( isDefined( obj.head ) )
    {
        obj.head unlink();
        obj.head delete();
    }
    if ( isDefined( obj.body ) )
    {
        obj.body unlink();
        obj.body delete();
    }
}

deleteObject( requestId, objectId )
{
    obj = level.iw4iris.objects[objectId];
    if ( isDefined( obj ) )
    {
        deleteParts( obj );
        level.iw4iris.objects[objectId] = undefined;
    }
    IW4IrisRespond( requestId, "ok", "" );
}

clearAll( requestId )
{
    keys = getArrayKeys( level.iw4iris.objects );
    for ( i = 0; i < keys.size; i++ )
        deleteParts( level.iw4iris.objects[keys[i]] );
    level.iw4iris.objects = [];
    IW4IrisRespond( requestId, "ok", "" );
}

setVisible( obj, visible )
{
    parts = [];
    parts[0] = obj.body;
    if ( isDefined( obj.head ) )
        parts[parts.size] = obj.head;
    if ( isDefined( obj.weapon ) )
        parts[parts.size] = obj.weapon;
    for ( i = 0; i < obj.attachments.size; i++ )
        parts[parts.size] = obj.attachments[i];

    for ( i = 0; i < parts.size; i++ )
    {
        if ( visible )
            parts[i] show();
        else
            parts[i] hide();
    }
}

// ----------------------------------------------------------------- per frame

// Flattened: id, x, y, z, pitch, yaw, roll, visible for each object, for
// the timeline time this server frame will be shown at.
applyPoses()
{
    poses = IW4IrisPoses( getTime() );
    for ( i = 0; i + 7 < poses.size; i += 8 )
    {
        obj = level.iw4iris.objects[poses[i]];
        if ( !isDefined( obj ) )
            continue;

        if ( !obj.linked )
        {
            obj.body.origin = ( poses[i + 1], poses[i + 2], poses[i + 3] );
            obj.body.angles = ( poses[i + 4], poses[i + 5], poses[i + 6] );
        }
        visible = poses[i + 7];
        if ( visible != obj.visible )
        {
            setVisible( obj, visible );
            obj.visible = visible;
        }
    }
}

// Flattened: name, origin xyz, forward xyz, up xyz for each effect due.
playEffects()
{
    events = IW4IrisEffects();
    for ( i = 0; i + 9 < events.size; i += 10 )
    {
        id = level.iw4iris.fx[events[i]];
        if ( !isDefined( id ) )
            continue;
        playFX( id, ( events[i + 1], events[i + 2], events[i + 3] ), ( events[i + 4], events[i + 5], events[i + 6] ), ( events[i + 7], events[i + 8], events[i + 9] ) );
    }
}

// Flattened: object id, tag. The editor only asks for bones every
// multiplayer body has (j_head, j_neck, j_spine4, j_mainroot,
// tag_weapon_right, tag_weapon_left), "tag_flash" of an attached weapon
// and "origin".
reportTags()
{
    requests = IW4IrisTagRequests();
    for ( i = 0; i + 1 < requests.size; i += 2 )
    {
        obj = level.iw4iris.objects[requests[i]];
        if ( !isDefined( obj ) )
            continue;

        tag = requests[i + 1];
        ent = obj.body;
        if ( tag == "tag_flash" )
        {
            if ( !isDefined( obj.weapon ) )
                continue;
            ent = obj.weapon;
        }
        if ( tag == "origin" )
        {
            pos = ent.origin;
            ang = ent.angles;
        }
        else
        {
            pos = ent getTagOrigin( tag );
            ang = ent getTagAngles( tag );
        }
        IW4IrisReportTag( requests[i], tag, pos[0], pos[1], pos[2], ang[0], ang[1], ang[2] );
    }
}
