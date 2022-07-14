#define PMF_PROMODE 0x8000
// TODO: needs to be moved to statIndex_t one day
#define STAT_JUMPTIME 10
#define STAT_DJING 11
// TODO: needs to be moved to surfaceflags.h or whatever
#define SURF_NOOB 0x80000


void PM_AddEvent( int newEvent ) {
    BG_AddPredictableEventToPlayerstate( newEvent, 0, pm->ps );
}

void PM_AddTouchEnt( int entityNum ) {
    int     i;

    if ( entityNum == ENTITYNUM_WORLD ) {
        return;
    }
    if ( pm->numtouch == MAXTOUCH ) {
        return;
    }

    // see if it is already added
    for ( i = 0 ; i < pm->numtouch ; i++ ) {
        if ( pm->touchents[ i ] == entityNum ) {
            return;
        }
    }

    // add it
    pm->touchents[pm->numtouch] = entityNum;
    pm->numtouch++;
}

static void PM_StartTorsoAnim( int anim ) {
    if ( pm->ps->pm_type >= PM_DEAD ) {
        return;
    }
    pm->ps->torsoAnim = ( ( pm->ps->torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
        | anim;
}

static void PM_StartLegsAnim( int anim ) {
    if ( pm->ps->pm_type >= PM_DEAD ) {
        return;
    }
    if ( pm->ps->legsTimer > 0 ) {
        return;     // a high priority animation is running
    }
    pm->ps->legsAnim = ( ( pm->ps->legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
        | anim;
}

static void PM_ContinueLegsAnim( int anim ) {
    if ( ( pm->ps->legsAnim & ~ANIM_TOGGLEBIT ) == anim ) {
        return;
    }
    if ( pm->ps->legsTimer > 0 ) {
        return;     // a high priority animation is running
    }
    PM_StartLegsAnim( anim );
}

static void PM_ContinueTorsoAnim( int anim ) {
    if ( ( pm->ps->torsoAnim & ~ANIM_TOGGLEBIT ) == anim ) {
        return;
    }
    if ( pm->ps->torsoTimer > 0 ) {
        return;     // a high priority animation is running
    }
    PM_StartTorsoAnim( anim );
}

static void PM_ForceLegsAnim( int anim ) {
    pm->ps->legsTimer = 0;
    PM_StartLegsAnim( anim );
}

void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
    float   backoff;
    float   change;
    int     i;

    backoff = DotProduct (in, normal);

    if ( backoff < 0 ) {
        backoff *= overbounce;
    } else {
        backoff /= overbounce;
    }

    for ( i=0 ; i<3 ; i++ ) {
        change = normal[i]*backoff;
        out[i] = in[i] - change;
    }
}

static void PM_Friction( void ) {
    vec3_t  vec;
    float   *vel;
    float   speed, newspeed, control;
    float   drop;

    vel = pm->ps->velocity;

    VectorCopy( vel, vec );
    if ( pml.walking ) {
        vec[2] = 0; // ignore slope movement
    }

    speed = VectorLength(vec);
    if (speed < 1) {
        vel[0] = 0;
        vel[1] = 0;     // allow sinking underwater
        // FIXME: still have z friction underwater?
        return;
    }

    drop = 0;

    // apply ground friction
    if ( pm->waterlevel <= 1 ) {
        if ( pml.walking && !(pml.groundTrace.surfaceFlags & SURF_SLICK) ) {
            // if getting knocked back, no friction
            if ( ! (pm->ps->pm_flags & PMF_TIME_KNOCKBACK) ) {
                control = speed < pm_stopspeed ? pm_stopspeed : speed;
                drop += control*pm_friction*pml.frametime;
            }
        }
    }

    // apply water friction even if just wading
    if ( pm->waterlevel ) {
        if ( pm->ps->pm_flags & PMF_PROMODE ) {
            drop += 0.5*speed*pm->waterlevel*pml.frametime;
        } else {
            drop += speed*pm_waterfriction*pm->waterlevel*pml.frametime;
        }
    }

    // apply flying friction
    if ( pm->ps->powerups[PW_FLIGHT]) {
        drop += speed*pm_flightfriction*pml.frametime;
    }

    if ( pm->ps->pm_type == PM_SPECTATOR) {
        drop += speed*pm_spectatorfriction*pml.frametime;
    }

    // scale the velocity
    newspeed = speed - drop;
    if (newspeed < 0) {
        newspeed = 0;
    }
    newspeed /= speed;

    vel[0] = vel[0] * newspeed;
    vel[1] = vel[1] * newspeed;
    vel[2] = vel[2] * newspeed;
}

static void PM_Accelerate( vec3_t wishdir, float wishspeed, float accel ) {
    // q2 style
    int         i;
    float       addspeed, accelspeed, currentspeed;

    currentspeed = DotProduct (pm->ps->velocity, wishdir);
    addspeed = wishspeed - currentspeed;
    if (addspeed <= 0) {
        return;
    }
    accelspeed = accel*pml.frametime*wishspeed;
    if (accelspeed > addspeed) {
        accelspeed = addspeed;
    }

    for (i=0 ; i<3 ; i++) {
        pm->ps->velocity[i] += accelspeed*wishdir[i];
    }
}

static float PM_CmdScale( usercmd_t *cmd ) {
    int     max;
    float   total;
    float   scale;

    max = abs( cmd->forwardmove );
    if ( abs( cmd->rightmove ) > max ) {
        max = abs( cmd->rightmove );
    }
    if ( abs( cmd->upmove ) > max ) {
        max = abs( cmd->upmove );
    }
    if ( !max ) {
        return 0;
    }

    total = sqrt( cmd->forwardmove * cmd->forwardmove
        + cmd->rightmove * cmd->rightmove + cmd->upmove * cmd->upmove );
    scale = (float)pm->ps->speed * max / ( 127.0 * total );

    return scale;
}

static void PM_SetMovementDir( void ) {
    if ( pm->cmd.forwardmove || pm->cmd.rightmove ) {
        if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove > 0 ) {
            pm->ps->movementDir = 0;
        } else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove > 0 ) {
            pm->ps->movementDir = 1;
        } else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove == 0 ) {
            pm->ps->movementDir = 2;
        } else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove < 0 ) {
            pm->ps->movementDir = 3;
        } else if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove < 0 ) {
            pm->ps->movementDir = 4;
        } else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove < 0 ) {
            pm->ps->movementDir = 5;
        } else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove == 0 ) {
            pm->ps->movementDir = 6;
        } else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove > 0 ) {
            pm->ps->movementDir = 7;
        }
    } else {
        // if they aren't actively going directly sideways,
        // change the animation to the diagonal so they
        // don't stop too crooked
        if ( pm->ps->movementDir == 2 ) {
            pm->ps->movementDir = 1;
        } else if ( pm->ps->movementDir == 6 ) {
            pm->ps->movementDir = 7;
        }
    }
}

static qboolean PM_CheckJump( void ) {
    if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
        return qfalse;      // don't allow jump until all buttons are up
    }

    if ( pm->cmd.upmove < 10 ) {
        // not holding jump
        return qfalse;
    }

    // must wait for jump to be released
    if ( pm->ps->pm_flags & PMF_JUMP_HELD ) {
        // clear upmove so cmdscale doesn't lower running speed
        pm->cmd.upmove = 0;
        return qfalse;
    }

    pml.groundPlane = qfalse;       // jumping away
    pml.walking = qfalse;
    pm->ps->pm_flags |= PMF_JUMP_HELD;
    pm->ps->groundEntityNum = ENTITYNUM_NONE;

    if ( pm->ps->velocity[2] <= 0 || !(pm->ps->pm_flags & PMF_PROMODE)) {
        pm->ps->velocity[2] = JUMP_VELOCITY;
    } else {
        pm->ps->velocity[2] += JUMP_VELOCITY;
    }

    if ( pm->ps->pm_flags & PMF_PROMODE ) {
        if ( pm->ps->stats[STAT_JUMPTIME] > 0 ) {
            pm->ps->velocity[2] += 100.0f;
            pm->ps->stats[STAT_DJING] = 1;
        }
        pm->ps->stats[STAT_JUMPTIME] = 400;
    }

    PM_AddEvent( EV_JUMP );

    if ( pm->cmd.forwardmove >= 0 ) {
        PM_ForceLegsAnim( LEGS_JUMP )
        pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
    } else {
        PM_ForceLegsAnim( LEGS_JUMPB )
        pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
    }

    return qtrue;
}

static qboolean PM_CheckWaterJump( void ) {
    vec3_t  spot;
    int     cont;
    vec3_t  flatforward;

    if (pm->ps->pm_time) {
        return qfalse;
    }

    // check for water jump
    if ( pm->waterlevel != 2 ) {
        return qfalse;
    }

    flatforward[0] = pml.forward[0];
    flatforward[1] = pml.forward[1];
    flatforward[2] = 0;
    VectorNormalize (flatforward);

    VectorMA (pm->ps->origin, 30, flatforward, spot);
    spot[2] += 4;
    cont = pm->pointcontents (spot, pm->ps->clientNum );
    if ( !(cont & CONTENTS_SOLID) ) {
        return qfalse;
    }

    spot[2] += 16;
    cont = pm->pointcontents (spot, pm->ps->clientNum );
    if ( cont ) {
        return qfalse;
    }

    // jump out of water
    VectorScale (pml.forward, 200, pm->ps->velocity);
    pm->ps->velocity[2] = 350;

    pm->ps->pm_flags |= PMF_TIME_WATERJUMP;
    pm->ps->pm_time = 2000;

    return qtrue;
}

static void PM_WaterJumpMove( void ) {
    // waterjump has no control, but falls

    PM_StepSlideMove( qtrue );

    pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
    if (pm->ps->velocity[2] < 0) {
        // cancel as soon as we are falling down again
        pm->ps->pm_flags &= ~PMF_ALL_TIMES;
        pm->ps->pm_time = 0;
    }
}

static void PM_WaterMove( void ) {
    int     i;
    vec3_t  wishvel;
    float   wishspeed;
    vec3_t  wishdir;
    float   scale;
    float   vel;

    if ( PM_CheckWaterJump() ) {
        PM_WaterJumpMove();
        return;
    }

    PM_Friction ();

    scale = PM_CmdScale( &pm->cmd );
    //
    // user intentions
    //
    if ( !scale ) {
        wishvel[0] = 0;
        wishvel[1] = 0;
        wishvel[2] = -60;       // sink towards bottom
    } else {
        for (i=0 ; i<3 ; i++)
            wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;

        wishvel[2] += scale * pm->cmd.upmove;
    }

    VectorCopy (wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);

    if(pm->ps->pm_flags & PMF_PROMODE)
    {
        if(pm->waterlevel == 1) {
            if(wishspeed > (0.585 * pm->ps->speed)) {
                wishspeed = 0.585 * pm->ps->speed;
            }
        } else if(wishspeed > (0.54 * pm->ps->speed)) {
            wishspeed = 0.54 * pm->ps->speed;
        }

        PM_Accelerate (wishdir, wishspeed, 5.0f);
    }
    else
    {
        if ( wishspeed > pm->ps->speed * pm_swimScale ) {
            wishspeed = pm->ps->speed * pm_swimScale;
        }

        PM_Accelerate (wishdir, wishspeed, pm_wateraccelerate);
    }

    // make sure we can go up slopes easily under water
    if ( pml.groundPlane && DotProduct( pm->ps->velocity, pml.groundTrace.plane.normal ) < 0 ) {
        vel = VectorLength(pm->ps->velocity);
        // slide along the ground plane
        PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal,
            pm->ps->velocity, OVERCLIP );

        VectorNormalize(pm->ps->velocity);
        VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
    }

    PM_SlideMove( qfalse );
}

static void PM_FlyMove( void ) {
    int     i;
    vec3_t  wishvel;
    float   wishspeed;
    vec3_t  wishdir;
    float   scale;

    // normal slowdown
    PM_Friction ();

    scale = PM_CmdScale( &pm->cmd );
    //
    // user intentions
    //
    if ( !scale ) {
        wishvel[0] = 0;
        wishvel[1] = 0;
        wishvel[2] = 0;
    } else {
        for (i=0 ; i<3 ; i++) {
            wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;
        }

        wishvel[2] += scale * pm->cmd.upmove;
    }

    VectorCopy (wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);

    PM_Accelerate (wishdir, wishspeed, pm_flyaccelerate);

    PM_StepSlideMove( qfalse );
}

// NOTE: This actually should be in some other file, but is only used here...
static void PM_AirControl(pmove_t *pm, vec3_t wishdir, float wishspeed) {
    float zspeed, speed, dot, k;

    if ( (pm->ps->movementDir && pm->ps->movementDir != 4) || wishspeed == 0.0 ) {
        return;
    }

    zspeed = pm->ps->velocity[2];
    pm->ps->velocity[2] = 0;
    speed = VectorNormalize( pm->ps->velocity );
    dot = DotProduct( pm->ps->velocity, wishdir );
    if ( dot > 0 ) {
        k = 32*150*dot*dot*pml.frametime;
        pm->ps->velocity[0] = pm->ps->velocity[0]*speed + wishdir[0]*k;
        pm->ps->velocity[1] = pm->ps->velocity[1]*speed + wishdir[1]*k;
        VectorNormalize( pm->ps->velocity );
    }

    pm->ps->velocity[0] *= speed;
    pm->ps->velocity[1] *= speed;
    pm->ps->velocity[2] = zspeed;
}

static void PM_AirMove( void ) {
    int         i;
    vec3_t      wishvel;
    float       fmove, smove;
    vec3_t      wishdir;
    float       wishspeed, wishspeed2;
    float       scale;
    usercmd_t   cmd;
    float       cpmairaccel;

    PM_Friction();

    fmove = pm->cmd.forwardmove;
    smove = pm->cmd.rightmove;

    cmd = pm->cmd;
    scale = PM_CmdScale( &cmd );

    // set the movementDir so clients can rotate the legs for strafing
    PM_SetMovementDir();

    // project moves down to flat plane
    pml.forward[2] = 0;
    pml.right[2] = 0;
    VectorNormalize (pml.forward);
    VectorNormalize (pml.right);

    for ( i = 0 ; i < 2 ; i++ ) {
        wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
    }
    wishvel[2] = 0;

    VectorCopy (wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);
    wishspeed *= scale;

    if ( (pm->ps->pm_flags & PMF_PROMODE) ) {
        wishspeed2 = wishspeed;
        cpmairaccel = pm_airaccelerate;
        if ( DotProduct(pm->ps->velocity, wishdir) < 0 ) {
            cpmairaccel = 2.5f;
        }
        if ( pm->ps->movementDir == 2 || pm->ps->movementDir == 6 ) {
            if ( wishspeed > 30.0f ) {
                wishspeed = 30.0f;
            }
            cpmairaccel = 70.0f;
        }
        PM_Accelerate( wishdir, wishspeed, cpmairaccel );
        PM_AirControl( pm, wishdir, wishspeed2 );
    } else {
        PM_Accelerate( wishdir, wishspeed, pm_airaccelerate );
    }

    // we may have a ground plane that is very steep, even
    // though we don't have a groundentity
    // slide along the steep plane
    if ( pml.groundPlane ) {
        PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal,
            pm->ps->velocity, OVERCLIP );
    }

    PM_StepSlideMove ( qtrue );
}

static void PM_GrappleMove( void ) {
    vec3_t vel, v;
    float vlen;

    VectorScale(pml.forward, -16, v);
    VectorAdd(pm->ps->grapplePoint, v, v);
    VectorSubtract(v, pm->ps->origin, vel);
    vlen = VectorLength(vel);
    VectorNormalize( vel );

    if (vlen <= 100)
        VectorScale(vel, 10 * vlen, vel);
    else
        VectorScale(vel, 800, vel);

    VectorCopy(vel, pm->ps->velocity);

    pml.groundPlane = qfalse;
}

static float PM_HookSpeed( void ) {
    float hook_speed = 800;

    if ( pm->ps->powerups[PW_HASTE] ) {
        hook_speed *= 1.3;
    }

    return hook_speed;
}

static void PM_Q2GrappleMove( void ) {
    vec3_t vel, v;
    float hook_speed, vlen;

    hook_speed = PM_HookSpeed();
    VectorScale(pml.forward, -16, v);
    VectorAdd(pm->ps->grapplePoint, v, v);
    VectorSubtract(v, pm->ps->origin, vel);
    vel[2] = (vel[2] - pm->ps->viewheight) - 4.0;
    vlen = VectorLength(vel);
    VectorNormalize( vel );

    if (vlen <= 100)
        VectorScale(vel, (hook_speed / 100.0) * vlen, vel);
    else
        VectorScale(vel, hook_speed, vel);

    VectorCopy(vel, pm->ps->velocity);

    pml.groundPlane = qfalse;
}

static void PM_SwingingGrappleMove( void ) {
    vec3_t hook_dir;
    float hook_dist, hook_speed;

    hook_speed = PM_HookSpeed();
    VectorSubtract(pm->ps->grapplePoint, pm->ps->origin, hook_dir);
    hook_dist = VectorLength( hook_dir );
    VectorNormalize( hook_dir );

    if ( hook_dist < (hook_speed / 2.0) ) {
        PM_Accelerate( hook_dir, 2.0 * hook_dist, hook_dist * (40.0 / hook_speed) );
    } else {
        PM_Accelerate( hook_dir, hook_speed, 20.0 );
    }

    if ( hook_dir[2] > 0.5f && pml.walking ) {
        pml.walking = qfalse;
        PM_ForceLegsAnim( LEGS_JUMP );
    }

    pml.groundPlane = qfalse;
}

static void PM_PendulumGrappleMove( void ) {
    // TODO: better name? it isn't normalized, so maybe dir isn't quite right
    vec3_t hook_dir;

    VectorSubtract( pm->ps->grapplePoint, pm->ps->origin, hook_dir );
    PM_Accelerate( hook_dir, 1.0, 5.0 );

    if ( hook_dir[2] > 0.5f && pml.walking ) {
        pml.walking = qfalse;
        PM_ForceLegsAnim( LEGS_JUMP );
    }

    pml.groundPlane = qfalse;
}

static void PM_DFGrappleMove( void ) {
    int hook_type = 0;
    if ( pm->ps->stats[12] & 0x400 ) hook_type += 1;
    if ( pm->ps->stats[12] & 0x800 ) hook_type += 2;

    switch( hook_type ) {
        default:
        case 0:
            PM_GrappleMove();
            PM_AirMove();
            break;

        case 1:
            PM_SwingingGrappleMove();
            if ( pml.walking ) {
                PM_WalkMove();
            } else {
                PM_AirMove();
            }
            break;

        case 2:
            PM_PendulumGrappleMove();
            if ( pml.walking ) {
                PM_WalkMove();
            } else {
                PM_AirMove();
            }
            break;

        case 3:
            PM_Q2GrappleMove();
            PM_AirMove();
            break;
    }
}

static void PM_WalkMove( void ) {
    int         i;
    vec3_t      wishvel;
    float       fmove, smove;
    vec3_t      wishdir;
    float       wishspeed;
    float       scale;
    usercmd_t   cmd;
    float       accelerate;
    float       vel;

    if ( pm->waterlevel > 2 && DotProduct( pml.forward, pml.groundTrace.plane.normal ) > 0 ) {
        // begin swimming
        PM_WaterMove();
        return;
    }

    if ( PM_CheckJump () ) {
        // jumped away
        if ( pm->waterlevel > 1 ) {
            PM_WaterMove();
        } else {
            PM_AirMove();
        }
        return;
    }


    PM_Friction();

    fmove = pm->cmd.forwardmove;
    smove = pm->cmd.rightmove;

    cmd = pm->cmd;
    scale = PM_CmdScale( &cmd );

    // set the movementDir so clients can rotate the legs for strafing
    PM_SetMovementDir();

    // project moves down to flat plane
    pml.forward[2] = 0;
    pml.right[2] = 0;

    // project the forward and right directions onto the ground plane
    PM_ClipVelocity (pml.forward, pml.groundTrace.plane.normal, pml.forward, OVERCLIP );
    PM_ClipVelocity (pml.right, pml.groundTrace.plane.normal, pml.right, OVERCLIP );
    //
    VectorNormalize (pml.forward);
    VectorNormalize (pml.right);

    for ( i = 0 ; i < 3 ; i++ ) {
        wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
    }

    VectorCopy (wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);
    wishspeed *= scale;

    if ( pm->ps->pm_flags & PMF_DUCKED ) {
        if ( wishspeed > pm->ps->speed * pm_duckScale ) {
            wishspeed = pm->ps->speed * pm_duckScale;
        }
    }

    if ( pm->waterlevel ) {
        float waterScale;

        if ( pm->ps->pm_flags & PMF_PROMODE ) {
            float cpm_swimScale;

            waterScale = pm->waterlevel / 3.0;
            if ( pm->waterlevel == 1 ) {
                cpm_swimScale = 0.585;
            } else {
                cpm_swimScale = 0.54;
            }
            waterScale = 1.0 - (1.0 - cpm_swimScale) * waterScale;
            if ( wishspeed > pm->ps->speed * waterScale ) {
                wishspeed = pm->ps->speed * waterScale;
            }
        } else {
            waterScale = pm->waterlevel / 3.0;
            waterScale = 1.0 - (1.0 - pm_swimScale) * waterScale;
            if ( wishspeed > pm->ps->speed * waterScale ) {
                wishspeed = pm->ps->speed * waterScale;
            }
        }
    }

    if ( pm->ps->pm_flags & PMF_PROMODE ) {
        accelerate = 15.0;
    } else {
        // when a player gets hit, they temporarily lose
        // full control, which allows them to be moved a bit
        if ( (pml.groundTrace.surfaceFlags & SURF_SLICK) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
            accelerate = pm_airaccelerate;
        } else {
            accelerate = pm_accelerate;
        }
    }

    PM_Accelerate( wishdir, wishspeed, accelerate );

    if ( (pml.groundTrace.surfaceFlags & SURF_SLICK) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
        pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
    }

    vel = VectorLength(pm->ps->velocity);

    // slide along the ground plane
    PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal,
        pm->ps->velocity, OVERCLIP );

    // don't decrease velocity when going up or down a slope
    VectorNormalize(pm->ps->velocity);
    VectorScale(pm->ps->velocity, vel, pm->ps->velocity);

    // don't do anything if standing still
    if (!pm->ps->velocity[0] && !pm->ps->velocity[1]) {
        return;
    }

    PM_StepSlideMove( qfalse );
}

static void PM_DeadMove( void ) {
    float   forward;

    if ( !pml.walking ) {
        return;
    }

    // extra friction

    forward = VectorLength (pm->ps->velocity);
    forward -= 20;
    if ( forward <= 0 ) {
        VectorClear (pm->ps->velocity);
    } else {
        VectorNormalize (pm->ps->velocity);
        VectorScale (pm->ps->velocity, forward, pm->ps->velocity);
    }
}

static void PM_NoclipMove( void ) {
    float   speed, drop, friction, control, newspeed;
    int         i;
    vec3_t      wishvel;
    float       fmove, smove;
    vec3_t      wishdir;
    float       wishspeed;
    float       scale;

    pm->ps->viewheight = DEFAULT_VIEWHEIGHT;

    // friction

    speed = VectorLength (pm->ps->velocity);
    if (speed < 1)
    {
        VectorCopy (vec3_origin, pm->ps->velocity);
    }
    else
    {
        drop = 0;

        friction = pm_friction*1.5; // extra friction
        control = speed < pm_stopspeed ? pm_stopspeed : speed;
        drop += control*friction*pml.frametime;

        // scale the velocity
        newspeed = speed - drop;
        if (newspeed < 0)
            newspeed = 0;
        newspeed /= speed;

        VectorScale (pm->ps->velocity, newspeed, pm->ps->velocity);
    }

    // accelerate
    scale = PM_CmdScale( &pm->cmd );

    fmove = pm->cmd.forwardmove;
    smove = pm->cmd.rightmove;

    for (i=0 ; i<3 ; i++)
        wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
    wishvel[2] += pm->cmd.upmove;

    VectorCopy (wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);
    wishspeed *= scale;

    PM_Accelerate( wishdir, wishspeed, pm_accelerate );

    // move
    VectorMA (pm->ps->origin, pml.frametime, pm->ps->velocity, pm->ps->origin);
}

static int PM_FootstepForSurface( void ) {
    if ( pml.groundTrace.surfaceFlags & SURF_NOSTEPS ) {
        return 0;
    }
    if ( pml.groundTrace.surfaceFlags & SURF_METALSTEPS ) {
        return EV_FOOTSTEP_METAL;
    }
    return EV_FOOTSTEP;
}

static void PM_CrashLand( void ) {
    float       delta;
    float       dist;
    float       vel, acc;
    float       t;
    float       a, b, c, den;

    // decide which landing animation to use
    if ( pm->ps->pm_flags & PMF_BACKWARDS_JUMP ) {
        PM_ForceLegsAnim( LEGS_LANDB );
    } else {
        PM_ForceLegsAnim( LEGS_LAND );
    }

    pm->ps->legsTimer = TIMER_LAND;

    // calculate the exact velocity on landing
    dist = pm->ps->origin[2] - pml.previous_origin[2];
    vel = pml.previous_velocity[2];
    acc = -pm->ps->gravity;

    a = acc / 2;
    b = vel;
    c = -dist;

    den =  b * b - 4 * a * c;
    if ( den < 0 ) {
        return;
    }
    t = (-b - sqrt( den ) ) / ( 2 * a );

    delta = vel + t * acc;
    delta = delta*delta * 0.0001;

    // ducking while falling doubles damage
    if ( pm->ps->pm_flags & PMF_DUCKED ) {
        delta *= 2;
    }

    // never take falling damage if completely underwater
    if ( pm->waterlevel == 3 ) {
        return;
    }

    // reduce falling damage if there is standing water
    if ( pm->waterlevel == 2 ) {
        delta *= 0.25;
    }
    if ( pm->waterlevel == 1 ) {
        delta *= 0.5;
    }

    if ( delta < 1 ) {
        return;
    }

    // create a local entity event to play the sound

    // SURF_NODAMAGE is used for bounce pads where you don't ever
    // want to take damage or play a crunch sound
    if ( !(pml.groundTrace.surfaceFlags & SURF_NODAMAGE) )  {
        if ( delta > 60 ) {
            PM_AddEvent( EV_FALL_FAR );
        } else if ( delta > 40 ) {
            // this is a pain grunt, so don't play it if dead
            if ( pm->ps->stats[STAT_HEALTH] > 0 ) {
                PM_AddEvent( EV_FALL_MEDIUM );
            }
        } else if ( delta > 7 ) {
            PM_AddEvent( EV_FALL_SHORT );
        } else {
            PM_AddEvent( PM_FootstepForSurface() );
        }
    }

    // start footstep cycle over
    pm->ps->bobCycle = 0;
}

static int PM_CorrectAllSolid( trace_t *trace ) {
    int         i, j, k;
    vec3_t      point;

    if ( pm->debugLevel ) {
        Com_Printf("%i:allsolid\n", c_pmove);
    }

    // jitter around
    for (i = -1; i <= 1; i++) {
        for (j = -1; j <= 1; j++) {
            for (k = -1; k <= 1; k++) {
                VectorCopy(pm->ps->origin, point);
                point[0] += (float) i;
                point[1] += (float) j;
                point[2] += (float) k;
                pm->trace (trace, point, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
                if ( !trace->allsolid ) {
                    point[0] = pm->ps->origin[0];
                    point[1] = pm->ps->origin[1];
                    point[2] = pm->ps->origin[2] - 0.25;

                    pm->trace (trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
                    pml.groundTrace = *trace;
                    return qtrue;
                }
            }
        }
    }

    pm->ps->groundEntityNum = ENTITYNUM_NONE;
    pml.groundPlane = qfalse;
    pml.walking = qfalse;

    return qfalse;
}

static void PM_GroundTraceMissed( void ) {
    trace_t     trace;
    vec3_t      point;

    if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
        // we just transitioned into freefall
        if ( pm->debugLevel ) {
            Com_Printf("%i:lift\n", c_pmove);
        }

        // if they aren't in a jumping animation and the ground is a ways away, force into it
        // if we didn't do the trace, the player would be backflipping down staircases
        VectorCopy( pm->ps->origin, point );
        point[2] -= 64;

        pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
        if ( trace.fraction == 1.0 ) {
            if ( pm->cmd.forwardmove >= 0 ) {
                PM_ForceLegsAnim( LEGS_JUMP );
                pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
            } else {
                PM_ForceLegsAnim( LEGS_JUMPB );
                pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
            }
        }
    }

    pm->ps->groundEntityNum = ENTITYNUM_NONE;
    pml.groundPlane = qfalse;
    pml.walking = qfalse;
}

static void PM_GroundTrace( void ) {
    vec3_t      point;
    trace_t     trace;

    point[0] = pm->ps->origin[0];
    point[1] = pm->ps->origin[1];
    point[2] = pm->ps->origin[2] - 0.25;

    pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, point, pm->ps->clientNum, pm->tracemask);
    pml.groundTrace = trace;

    // do something corrective if the trace starts in a solid...
    if ( trace.allsolid ) {
        if ( !PM_CorrectAllSolid(&trace) )
            return;
    }

    // if the trace didn't hit anything, we are in free fall
    if ( trace.fraction == 1.0 ) {
        PM_GroundTraceMissed();
        pml.groundPlane = qfalse;
        pml.walking = qfalse;
        return;
    }

    // check if getting thrown off the ground
    if ( pm->ps->velocity[2] > 0 && DotProduct( pm->ps->velocity, trace.plane.normal ) > 10 ) {
        if ( pm->debugLevel ) {
            Com_Printf("%i:kickoff\n", c_pmove);
        }
        // go into jump animation
        if ( pm->cmd.forwardmove >= 0 ) {
            PM_ForceLegsAnim( LEGS_JUMP );
            pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
        } else {
            PM_ForceLegsAnim( LEGS_JUMPB );
            pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
        }

        pm->ps->groundEntityNum = ENTITYNUM_NONE;
        pml.groundPlane = qfalse;
        pml.walking = qfalse;
        return;
    }

    // slopes that are too steep will not be considered onground
    if ( trace.plane.normal[2] < MIN_WALK_NORMAL ) {
        if ( pm->debugLevel ) {
            Com_Printf("%i:steep\n", c_pmove);
        }
        // FIXME: if they can't slide down the slope, let them
        // walk (sharp crevices)
        pm->ps->groundEntityNum = ENTITYNUM_NONE;
        pml.groundPlane = qtrue;
        pml.walking = qfalse;
        return;
    }

    pml.groundPlane = qtrue;
    pml.walking = qtrue;

    // hitting solid ground will end a waterjump
    if (pm->ps->pm_flags & PMF_TIME_WATERJUMP)
    {
        pm->ps->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND);
        pm->ps->pm_time = 0;
    }

    if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
        // just hit the ground
        if ( pm->debugLevel ) {
            Com_Printf("%i:Land\n", c_pmove);
        }

        PM_CrashLand();

        if ( (pm->ps->stats[12] & 0x200) || (pml.groundTrace.surfaceFlags & SURF_NOOB) ) {
            PM_ClipVelocity(pm->ps->velocity, trace.plane.normal, pm->ps->velocity, OVERCLIP);
        }

        // don't do landing time if we were just going down a slope
        if ( pml.previous_velocity[2] < -200 ) {
            // don't allow another jump for a little while
            pm->ps->pm_flags |= PMF_TIME_LAND;
            pm->ps->pm_time = 250;
        }
    }

    pm->ps->groundEntityNum = trace.entityNum;

    if ( ((pm->ps->stats[12] & 0x200) || (pml.groundTrace.surfaceFlags & SURF_NOOB)) && trace.plane.normal[2] == 1.0f ) {
        pm->ps->velocity[2] = 0;
    }

    // don't reset the z velocity for slopes
//  pm->ps->velocity[2] = 0;

    PM_AddTouchEnt( trace.entityNum );
}

static void PM_SetWaterLevel( void ) {
    vec3_t      point;
    int         cont;
    int         sample1;
    int         sample2;

    //
    // get waterlevel, accounting for ducking
    //
    pm->waterlevel = 0;
    pm->watertype = 0;

    point[0] = pm->ps->origin[0];
    point[1] = pm->ps->origin[1];
    point[2] = pm->ps->origin[2] + MINS_Z + 1;
    cont = pm->pointcontents( point, pm->ps->clientNum );

    if ( cont & MASK_WATER ) {
        sample2 = pm->ps->viewheight - MINS_Z;
        sample1 = sample2 / 2;

        pm->watertype = cont;
        pm->waterlevel = 1;
        point[2] = pm->ps->origin[2] + MINS_Z + sample1;
        cont = pm->pointcontents (point, pm->ps->clientNum );
        if ( cont & MASK_WATER ) {
            pm->waterlevel = 2;
            point[2] = pm->ps->origin[2] + MINS_Z + sample2;
            cont = pm->pointcontents (point, pm->ps->clientNum );
            if ( cont & MASK_WATER ){
                pm->waterlevel = 3;
            }
        }
    }

}

static void PM_CheckDuck (void)
{
    trace_t trace;

    if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
        if ( pm->ps->pm_flags & PMF_INVULEXPAND ) {
            // invulnerability sphere has a 42 units radius
            VectorSet( pm->mins, -42, -42, -42 );
            VectorSet( pm->maxs, 42, 42, 42 );
        }
        else {
            VectorSet( pm->mins, -15, -15, MINS_Z );
            VectorSet( pm->maxs, 15, 15, 16 );
        }
        pm->ps->pm_flags |= PMF_DUCKED;
        pm->ps->viewheight = CROUCH_VIEWHEIGHT;
        return;
    }
    pm->ps->pm_flags &= ~PMF_INVULEXPAND;

    pm->mins[0] = -15;
    pm->mins[1] = -15;

    pm->maxs[0] = 15;
    pm->maxs[1] = 15;

    pm->mins[2] = MINS_Z;

    if (pm->ps->pm_type == PM_DEAD)
    {
        pm->maxs[2] = -8;
        pm->ps->viewheight = DEAD_VIEWHEIGHT;
        return;
    }

    if (pm->cmd.upmove < 0)
    {   // duck
        pm->ps->pm_flags |= PMF_DUCKED;
    }
    else
    {   // stand up if possible
        if (pm->ps->pm_flags & PMF_DUCKED)
        {
            // try to stand up
            pm->maxs[2] = 32;
            pm->trace (&trace, pm->ps->origin, pm->mins, pm->maxs, pm->ps->origin, pm->ps->clientNum, pm->tracemask );
            if (!trace.allsolid)
                pm->ps->pm_flags &= ~PMF_DUCKED;
        }
    }

    if (pm->ps->pm_flags & PMF_DUCKED)
    {
        pm->maxs[2] = 16;
        pm->ps->viewheight = CROUCH_VIEWHEIGHT;
    }
    else
    {
        pm->maxs[2] = 32;
        pm->ps->viewheight = DEFAULT_VIEWHEIGHT;
    }
}

static void PM_Footsteps( void ) {
    float       bobmove;
    int         old;
    qboolean    footstep;

    //
    // calculate speed and cycle to be used for
    // all cyclic walking effects
    //
    pm->xyspeed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
        +  pm->ps->velocity[1] * pm->ps->velocity[1] );

    if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {

        if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
            PM_ContinueLegsAnim( LEGS_IDLECR );
        }
        // airborne leaves position in cycle intact, but doesn't advance
        if ( pm->waterlevel > 1 ) {
            PM_ContinueLegsAnim( LEGS_SWIM );
        }
        return;
    }

    // if not trying to move
    if ( !pm->cmd.forwardmove && !pm->cmd.rightmove ) {
        if (  pm->xyspeed < 5 ) {
            pm->ps->bobCycle = 0;   // start at beginning of cycle again
            if ( pm->ps->pm_flags & PMF_DUCKED ) {
                PM_ContinueLegsAnim( LEGS_IDLECR );
            } else {
                PM_ContinueLegsAnim( LEGS_IDLE );
            }
        }
        return;
    }


    footstep = qfalse;

    if ( pm->ps->pm_flags & PMF_DUCKED ) {
        bobmove = 0.5;  // ducked characters bob much faster
        if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
            PM_ContinueLegsAnim( LEGS_BACKCR );
        }
        else {
            PM_ContinueLegsAnim( LEGS_WALKCR );
        }
        // ducked characters never play footsteps
    /*
    } else  if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
        if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
            bobmove = 0.4;  // faster speeds bob faster
            footstep = qtrue;
        } else {
            bobmove = 0.3;
        }
        PM_ContinueLegsAnim( LEGS_BACK );
    */
    } else {
        if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
            bobmove = 0.4f; // faster speeds bob faster
            if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
                PM_ContinueLegsAnim( LEGS_BACK );
            }
            else {
                PM_ContinueLegsAnim( LEGS_RUN );
            }
            footstep = qtrue;
        } else {
            bobmove = 0.3f; // walking bobs slow
            if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
                PM_ContinueLegsAnim( LEGS_BACKWALK );
            }
            else {
                PM_ContinueLegsAnim( LEGS_WALK );
            }
        }
    }

    // check for footstep / splash sounds
    old = pm->ps->bobCycle;
    pm->ps->bobCycle = (int)( old + bobmove * pml.msec ) & 255;

    // if we just crossed a cycle boundary, play an apropriate footstep event
    if ( ( ( old + 64 ) ^ ( pm->ps->bobCycle + 64 ) ) & 128 ) {
        if ( pm->waterlevel == 0 ) {
            // on ground will only play sounds if running
            if ( footstep && !pm->noFootsteps ) {
                PM_AddEvent( PM_FootstepForSurface() );
            }
        } else if ( pm->waterlevel == 1 ) {
            // splashing
            PM_AddEvent( EV_FOOTSPLASH );
        } else if ( pm->waterlevel == 2 ) {
            // wading / swimming at surface
            PM_AddEvent( EV_SWIM );
        } else if ( pm->waterlevel == 3 ) {
            // no sound when completely underwater

        }
    }
}

static void PM_WaterEvents( void ) {        // FIXME?
    //
    // if just entered a water volume, play a sound
    //
    if (!pml.previous_waterlevel && pm->waterlevel) {
        PM_AddEvent( EV_WATER_TOUCH );
    }

    //
    // if just completely exited a water volume, play a sound
    //
    if (pml.previous_waterlevel && !pm->waterlevel) {
        PM_AddEvent( EV_WATER_LEAVE );
    }

    //
    // check for head just going under water
    //
    if (pml.previous_waterlevel != 3 && pm->waterlevel == 3) {
        PM_AddEvent( EV_WATER_UNDER );
    }

    //
    // check for head just coming out of water
    //
    if (pml.previous_waterlevel == 3 && pm->waterlevel != 3) {
        PM_AddEvent( EV_WATER_CLEAR );
    }
}

static void PM_BeginWeaponChange( int weapon ) {
    if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) {
        return;
    }

    if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
        return;
    }

    if ( weapon == WP_GRAPPLING_HOOK ) {
        return;
    }

    if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
        return;
    }

    PM_AddEvent( EV_CHANGE_WEAPON );
    pm->ps->weaponstate = WEAPON_DROPPING;

    if ( pm->ps->pm_flags & PMF_PROMODE ) {
        // did someone really write this?
        // maybe the if-else is supposed to be ternary?
        pm->ps->weaponTime = pm->ps->weaponTime;
    } else {
        pm->ps->weaponTime += 200;
    }

    PM_StartTorsoAnim( TORSO_DROP );
}

static void PM_FinishWeaponChange( void ) {
    int     weapon;

    weapon = pm->cmd.weapon;
    if ( weapon < WP_NONE || weapon >= WP_NUM_WEAPONS ) {
        weapon = WP_NONE;
    }

    if ( !( pm->ps->stats[STAT_WEAPONS] & ( 1 << weapon ) ) ) {
        weapon = WP_NONE;
    }

    pm->ps->weapon = weapon;
    pm->ps->weaponstate = WEAPON_RAISING;

    if ( pm->ps->pm_flags & PMF_PROMODE ) {
        // did someone really write this?
        // maybe the if-else is supposed to be ternary?
        pm->ps->weaponTime = pm->ps->weaponTime;
    } else {
        pm->ps->weaponTime += 250;
    }

    PM_StartTorsoAnim( TORSO_RAISE );
}

static void PM_TorsoAnimation( void ) {
    if ( pm->ps->weaponstate == WEAPON_READY ) {
        if ( pm->ps->weapon == WP_GAUNTLET ) {
            PM_ContinueTorsoAnim( TORSO_STAND2 );
        } else {
            PM_ContinueTorsoAnim( TORSO_STAND );
        }
        return;
    }
}

static void PM_Weapon( void ) {
    int     addTime;

    // don't allow attack until all buttons are up
    if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
        return;
    }

    // ignore if spectator
    if ( pm->ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
        return;
    }

    // check for dead player
    if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
        pm->ps->weapon = WP_NONE;
        return;
    }

    // check for item using
    if ( pm->cmd.buttons & BUTTON_USE_HOLDABLE ) {
        if ( ! ( pm->ps->pm_flags & PMF_USE_ITEM_HELD ) ) {
            if ( bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag == HI_MEDKIT
                && pm->ps->stats[STAT_HEALTH] >= (pm->ps->stats[STAT_MAX_HEALTH] + 25) ) {
                // don't use medkit if at max health
            } else {
                pm->ps->pm_flags |= PMF_USE_ITEM_HELD;
                PM_AddEvent( EV_USE_ITEM0 + bg_itemlist[pm->ps->stats[STAT_HOLDABLE_ITEM]].giTag );
                pm->ps->stats[STAT_HOLDABLE_ITEM] = 0;
            }
            return;
        }
    } else {
        pm->ps->pm_flags &= ~PMF_USE_ITEM_HELD;
    }


    // make weapon function
    if ( pm->ps->weaponTime > 0 ) {
        pm->ps->weaponTime -= pml.msec;
    }

    // check for weapon change
    // can't change if weapon is firing, but can change
    // again if lowering or raising
    if ( pm->ps->weaponTime <= 0 || pm->ps->weaponstate != WEAPON_FIRING ) {
        if ( pm->ps->weapon != pm->cmd.weapon ) {
            PM_BeginWeaponChange( pm->cmd.weapon );
        }
    }

    if ( pm->ps->weaponTime > 0 ) {
        return;
    }

    // change weapon if time
    if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
        PM_FinishWeaponChange();
        return;
    }

    if ( pm->ps->weaponstate == WEAPON_RAISING ) {
        pm->ps->weaponstate = WEAPON_READY;
        if ( pm->ps->weapon == WP_GAUNTLET ) {
            PM_StartTorsoAnim( TORSO_STAND2 );
        } else {
            PM_StartTorsoAnim( TORSO_STAND );
        }
        return;
    }

    // check for fire
    if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
        pm->ps->weaponTime = 0;
        pm->ps->weaponstate = WEAPON_READY;
        return;
    }

    // start the animation even if out of ammo
    if ( pm->ps->weapon == WP_GAUNTLET ) {
        // the guantlet only "fires" when it actually hits something
        if ( !pm->gauntletHit ) {
            pm->ps->weaponTime = 0;
            pm->ps->weaponstate = WEAPON_READY;
            return;
        }
        PM_StartTorsoAnim( TORSO_ATTACK2 );
    } else {
        PM_StartTorsoAnim( TORSO_ATTACK );
    }

    pm->ps->weaponstate = WEAPON_FIRING;

    // check for out of ammo
    if ( ! pm->ps->ammo[ pm->ps->weapon ] ) {
        PM_AddEvent( EV_NOAMMO );
        pm->ps->weaponTime += 500;
        return;
    }

    // take an ammo away if not infinite
    if ( pm->ps->ammo[ pm->ps->weapon ] != -1 ) {
        pm->ps->ammo[ pm->ps->weapon ]--;
    }

    // fire weapon
    PM_AddEvent( EV_FIRE_WEAPON );

    switch( pm->ps->weapon ) {
    default:
    case WP_GAUNTLET:
        addTime = 400;
        break;
    case WP_LIGHTNING:
        addTime = 50;
        break;
    case WP_SHOTGUN:
        addTime = 1000;
        break;
    case WP_MACHINEGUN:
        addTime = 100;
        break;
    case WP_GRENADE_LAUNCHER:
        addTime = 800;
        break;
    case WP_ROCKET_LAUNCHER:
        addTime = 800;
        break;
    case WP_PLASMAGUN:
        addTime = 100;
        break;
    case WP_RAILGUN:
        addTime = 1500;
        break;
    case WP_BFG:
        addTime = 200;
        break;
    case WP_GRAPPLING_HOOK:
        addTime = 400;
        break;
#ifdef MISSIONPACK
    case WP_NAILGUN:
        addTime = 1000;
        break;
    case WP_PROX_LAUNCHER:
        addTime = 800;
        break;
    case WP_CHAINGUN:
        addTime = 30;
        break;
#endif
    }

#ifdef MISSIONPACK
    if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
        addTime /= 1.5;
    }
    else
    if( bg_itemlist[pm->ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_AMMOREGEN ) {
        addTime /= 1.3;
  }
  else
#endif
    if ( pm->ps->powerups[PW_HASTE] ) {
        addTime /= 1.3;
    }

    pm->ps->weaponTime += addTime;
}

static void PM_Animate( void ) {
    if ( pm->cmd.buttons & BUTTON_GESTURE ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_GESTURE );
            pm->ps->torsoTimer = TIMER_GESTURE;
            PM_AddEvent( EV_TAUNT );
        }
#ifdef MISSIONPACK
    } else if ( pm->cmd.buttons & BUTTON_GETFLAG ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_GETFLAG );
            pm->ps->torsoTimer = 600;   //TIMER_GESTURE;
        }
    } else if ( pm->cmd.buttons & BUTTON_GUARDBASE ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_GUARDBASE );
            pm->ps->torsoTimer = 600;   //TIMER_GESTURE;
        }
    } else if ( pm->cmd.buttons & BUTTON_PATROL ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_PATROL );
            pm->ps->torsoTimer = 600;   //TIMER_GESTURE;
        }
    } else if ( pm->cmd.buttons & BUTTON_FOLLOWME ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_FOLLOWME );
            pm->ps->torsoTimer = 600;   //TIMER_GESTURE;
        }
    } else if ( pm->cmd.buttons & BUTTON_AFFIRMATIVE ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_AFFIRMATIVE);
            pm->ps->torsoTimer = 600;   //TIMER_GESTURE;
        }
    } else if ( pm->cmd.buttons & BUTTON_NEGATIVE ) {
        if ( pm->ps->torsoTimer == 0 ) {
            PM_StartTorsoAnim( TORSO_NEGATIVE );
            pm->ps->torsoTimer = 600;   //TIMER_GESTURE;
        }
#endif
    }
}

static void PM_DropTimers( void ) {
    // drop misc timing counter
    if ( pm->ps->pm_time ) {
        if ( pml.msec >= pm->ps->pm_time ) {
            pm->ps->pm_flags &= ~PMF_ALL_TIMES;
            pm->ps->pm_time = 0;
        } else {
            pm->ps->pm_time -= pml.msec;
        }
    }

    // drop animation counter
    if ( pm->ps->legsTimer > 0 ) {
        pm->ps->legsTimer -= pml.msec;
        if ( pm->ps->legsTimer < 0 ) {
            pm->ps->legsTimer = 0;
        }
    }

    if ( pm->ps->torsoTimer > 0 ) {
        pm->ps->torsoTimer -= pml.msec;
        if ( pm->ps->torsoTimer < 0 ) {
            pm->ps->torsoTimer = 0;
        }
    }
}

void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd ) {
    short       temp;
    int     i;

    if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPINTERMISSION) {
        return;     // no view changes at all
    }

    if ( ps->pm_type != PM_SPECTATOR && ps->stats[STAT_HEALTH] <= 0 ) {
        return;     // no view changes at all
    }

    // circularly clamp the angles with deltas
    for (i=0 ; i<3 ; i++) {
        temp = cmd->angles[i] + ps->delta_angles[i];
        if ( i == PITCH ) {
            // don't let the player look up or down more than 90 degrees
            if ( temp > 16000 ) {
                ps->delta_angles[i] = 16000 - cmd->angles[i];
                temp = 16000;
            } else if ( temp < -16000 ) {
                ps->delta_angles[i] = -16000 - cmd->angles[i];
                temp = -16000;
            }
        }
        ps->viewangles[i] = SHORT2ANGLE(temp);
    }

}

// TODO: name this better maybe
// NOTE: This actually should be in some other file, but is only used here...
qboolean is_walking( pmove_t *pmove ) {
    // return abs( pmove->cmd.forwardmove ) == 64 || abs( pmove->cmd.rightmove ) == 64;
    // but disassembly looks like it maybe be something like this

    if ( abs( pmove->cmd.forwardmove ) == 64 ) {
        return qtrue;
    } else if ( abs( pmove->cmd.rightmove ) == 64 ) {
        return qtrue;
    } else {
        return qfalse;
    }
}

// TODO: name this better maybe
// NOTE: This actually should be in some other file, but is only used here...
void set_stats_13( pmove_t *pmove ) {
    // TODO: name stats[13] and the flags it contains
    pmove->ps->stats[13] = 0;
    if ( pmove->cmd.forwardmove > 0 ) {
        pmove->ps->stats[13] |= 1;
    }
    if ( pmove->cmd.forwardmove < 0 ) {
        pmove->ps->stats[13] |= 2;
    }
    if ( pmove->cmd.rightmove < 0 ) {
        pmove->ps->stats[13] |= 8;
    }
    if ( pmove->cmd.rightmove > 0 ) {
        pmove->ps->stats[13] |= 0x10;
    }
    if ( pmove->cmd.upmove > 0 ) {
        pmove->ps->stats[13] |= 0x20;
    }
    if ( pmove->cmd.upmove < 0 ) {
        pmove->ps->stats[13] |= 0x40;
    }
    if ( pmove->cmd.buttons & 1 ) {
        pmove->ps->stats[13] |= 0x100;
    }
    if ( is_walking( pmove ) ) {
        pmove->ps->stats[13] |= 0x200;
    }
}

void PmoveSingle (pmove_t *pmove) {
    set_stats_13();

    pm = pmove;

    // this counter lets us debug movement problems with a journal
    // by setting a conditional breakpoint fot the previous frame
    c_pmove++;

    // clear results
    pm->numtouch = 0;
    pm->watertype = 0;
    pm->waterlevel = 0;

    if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
        pm->tracemask &= ~CONTENTS_BODY;    // corpses can fly through bodies
    }

    // make sure walking button is clear if they are running, to avoid
    // proxy no-footsteps cheats
    if ( abs( pm->cmd.forwardmove ) > 64 || abs( pm->cmd.rightmove ) > 64 ) {
        pm->cmd.buttons &= ~BUTTON_WALKING;
    }

    // set the talk balloon flag
    if ( pm->cmd.buttons & BUTTON_TALK ) {
        pm->ps->eFlags |= EF_TALK;
    } else {
        pm->ps->eFlags &= ~EF_TALK;
    }

    // set the firing flag for continuous beam weapons
    if ( !(pm->ps->pm_flags & PMF_RESPAWNED) && pm->ps->pm_type != PM_INTERMISSION
        && ( pm->cmd.buttons & BUTTON_ATTACK ) && pm->ps->ammo[ pm->ps->weapon ] ) {
        pm->ps->eFlags |= EF_FIRING;
    } else {
        pm->ps->eFlags &= ~EF_FIRING;
    }

    // clear the respawned flag if attack and use are cleared
    if ( pm->ps->stats[STAT_HEALTH] > 0 &&
        !( pm->cmd.buttons & (BUTTON_ATTACK | BUTTON_USE_HOLDABLE) ) ) {
        pm->ps->pm_flags &= ~PMF_RESPAWNED;
    }

    // if talk button is down, dissallow all other input
    // this is to prevent any possible intercept proxy from
    // adding fake talk balloons
    if ( pmove->cmd.buttons & BUTTON_TALK ) {
        // keep the talk button set tho for when the cmd.serverTime > 66 msec
        // and the same cmd is used multiple times in Pmove
        pmove->cmd.buttons = BUTTON_TALK;
        pmove->cmd.forwardmove = 0;
        pmove->cmd.rightmove = 0;
        pmove->cmd.upmove = 0;
    }

    // clear all pmove local vars
    memset (&pml, 0, sizeof(pml));

    // determine the time
    pml.msec = pmove->cmd.serverTime - pm->ps->commandTime;
    if ( pml.msec < 1 ) {
        pml.msec = 1;
    } else if ( pml.msec > 200 ) {
        pml.msec = 200;
    }
    pm->ps->commandTime = pmove->cmd.serverTime;

    // save old org in case we get stuck
    VectorCopy (pm->ps->origin, pml.previous_origin);

    // save old velocity for crashlanding
    VectorCopy (pm->ps->velocity, pml.previous_velocity);

    pml.frametime = pml.msec * 0.001;

    // update the viewangles
    PM_UpdateViewAngles( pm->ps, &pm->cmd );

    AngleVectors (pm->ps->viewangles, pml.forward, pml.right, pml.up);

    if ( pm->cmd.upmove < 10 ) {
        // not holding jump
        pm->ps->pm_flags &= ~PMF_JUMP_HELD;
    }

    // decide if backpedaling animations should be used
    if ( pm->cmd.forwardmove < 0 ) {
        pm->ps->pm_flags |= PMF_BACKWARDS_RUN;
    } else if ( pm->cmd.forwardmove > 0 || ( pm->cmd.forwardmove == 0 && pm->cmd.rightmove ) ) {
        pm->ps->pm_flags &= ~PMF_BACKWARDS_RUN;
    }

    if ( pm->ps->pm_type >= PM_DEAD ) {
        pm->cmd.forwardmove = 0;
        pm->cmd.rightmove = 0;
        pm->cmd.upmove = 0;
    }

    if ( pm->ps->pm_type == PM_SPECTATOR ) {
        PM_CheckDuck ();
        PM_FlyMove ();
        PM_DropTimers ();
        return;
    }

    if ( pm->ps->pm_type == PM_NOCLIP ) {
        PM_NoclipMove ();
        PM_DropTimers ();
        return;
    }

    if (pm->ps->pm_type == PM_FREEZE) {
        return;     // no movement at all
    }

    if ( pm->ps->pm_type == PM_INTERMISSION || pm->ps->pm_type == PM_SPINTERMISSION) {
        return;     // no movement at all
    }

    // set watertype, and waterlevel
    PM_SetWaterLevel();
    pml.previous_waterlevel = pmove->waterlevel;

    // set mins, maxs, and viewheight
    PM_CheckDuck ();

    // set groundentity
    PM_GroundTrace();

    if ( pm->ps->pm_type == PM_DEAD ) {
        PM_DeadMove ();
    }

    PM_DropTimers();

    if ( pm->ps->pm_flags & PMF_PROMODE ) {
        if ( pm->ps->stats[STAT_JUMPTIME] > 0 ) {
            pm->ps->stats[STAT_JUMPTIME] -= pml.msec;
        } else if ( pm->ps->stats[STAT_DJING] ) {
            pm->ps->stats[STAT_DJING] = 0;
        }
    }

#ifdef MISSIONPACK
    if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
        PM_InvulnerabilityMove();
    } else
#endif
    if ( pm->ps->powerups[PW_FLIGHT] ) {
        // flight powerup doesn't allow jump and has different friction
        PM_FlyMove();
    } else if (pm->ps->pm_flags & PMF_GRAPPLE_PULL) {
        PM_DFGrappleMove();
    } else if (pm->ps->pm_flags & PMF_TIME_WATERJUMP) {
        PM_WaterJumpMove();
    } else if ( pm->waterlevel > 1 ) {
        // swimming
        PM_WaterMove();
    } else if ( pml.walking ) {
        // walking on ground
        PM_WalkMove();
    } else {
        // airborne
        PM_AirMove();
    }

    PM_Animate();

    // set groundentity, watertype, and waterlevel
    PM_GroundTrace();
    PM_SetWaterLevel();

    // weapons
    PM_Weapon();

    // torso animation
    PM_TorsoAnimation();

    // footstep events / legs animations
    PM_Footsteps();

    // entering / leaving water splashes
    PM_WaterEvents();

    // snap some parts of playerstate to save network bandwidth
    trap_SnapVector( pm->ps->velocity );
}

void Pmove (pmove_t *pmove) {
    int         finalTime;

    finalTime = pmove->cmd.serverTime;

    if ( finalTime < pmove->ps->commandTime || pmove->ps->commandTime == 0 ) {
        pmove->mins[0] = -15.0;
        pmove->mins[1] = -15.0;
        pmove->maxs[0] = 15.0;
        pmove->maxs[1] = 15.0;
        pmove->mins[2] = -24.0;
        pmove->maxs[2] = 32.0;
        return;
    }

    if ( finalTime > pmove->ps->commandTime + 1000 ) {
        pmove->ps->commandTime = finalTime;
    }

    pmove->ps->pmove_framecount = (pmove->ps->pmove_framecount+1) & ((1<<PS_PMOVEFRAMECOUNTBITS)-1);

    // chop the move up if it is too long, to prevent framerate
    // dependent behavior
    while ( pmove->ps->commandTime != finalTime ) {
        int     msec;

        msec = finalTime - pmove->ps->commandTime;

        if ( pmove->pmove_fixed ) {
            if ( msec > pmove->pmove_msec ) {
                msec = pmove->pmove_msec;
            }
        }
        else {
            if ( msec > 66 ) {
                msec = 66;
            }
        }
        pmove->cmd.serverTime = pmove->ps->commandTime + msec;
        PmoveSingle( pmove );

        if ( pmove->ps->pm_flags & PMF_JUMP_HELD ) {
            pmove->cmd.upmove = 20;
        }
    }

    //PM_CheckStuck();

}

