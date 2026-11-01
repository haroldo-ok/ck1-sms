/* Enemy / projectile / rope code.  Compiled into its own switchable ROM bank
 * (see the Makefile) because the fixed ROM is full; main.c pages it into slot 2
 * for one tick at a time (ai_run).  Engine state is shared through shared.h,
 * which is generated from main.c's declarations.  While this code runs slot 2
 * holds the AI bank, so anything that reads the level's map or tile data must
 * map the level bank in around the call (ai_lv, captured by ai_tick). */
#include "shared.h"
/* ---- AI bank: everything below lives in its own switchable bank (slot 2) --
   The fixed ROM is full.  While this code runs slot 2 holds the AI bank, so
   anything that reads the level's map or tile data must map the level bank
   in around the call (ai_lv is the level bank, captured by ai_tick). */
/* ---------------------------------------------------------------- bullet --- */
/* hit boxes (x1,y1,x2,y2) inside the drawn box, from the EGASPRIT sprite
   table; the butler's 16x16 art sits in the bottom of a 16x24 box */
static const signed char hbox[5][4] = {
    { 4, 4, 11, 23 },     /* yorp     */
    { 4, 4, 19, 31 },     /* garg     */
    { 2, 1, 21, 31 },     /* vorticon */
    { 3, 11, 12, 23 },    /* butler   */
    { 0, 0, 15, 23 },     /* tank     */
};
static void rope_start(void);
static void spawn_ray(int x, int y, unsigned char right);
/* is a world point inside CG's visibility window (screen + 3 tiles)? */
static unsigned char on_screen_pt(int x, int y) {
    return (unsigned int)(x - (int)cam_x + 48) <= 352 &&
           (unsigned int)(y - (int)cam_y + 48) <= 288;
}

/* Keen's ray (CG CRay): flies 10 px/tick, absorbed by walls, damages
   anything with canbezapped (CG getShotByRay: hp-1 unless invincible),
   and destroys an enemy ray it meets. Butlers and tanks absorb it. */
static void ent_shot(Ent *e) {
    if (e->type >= ET_BUTLER) return;               /* invincible */
    if (e->hp) e->hp--;
}

static void bullet_update(void) {
    unsigned char i;
    if (!b_active) return;
    if (b_hit) {
        if (++b_timer > 8) b_active = 0;
        return;
    }
    b_px += b_right ? (BULLET_V >> 8) : -(BULLET_V >> 8);
    /* out of view */
    if (b_px < (int)cam_x - 24 || b_px > (int)cam_x + 272) { b_active = 0; return; }
    /* wall */
    if (mflag_a(b_px + 8, b_py + 4) & (b_right ? F_BLKR : F_BLKL)) {
        b_hit = 1; b_timer = 0;
        sfx_play(SND_SHOTHIT);
        return;
    }
    /* entities (the zap is on screen, so only near ones can be hit) */
    for (i = 0; i < n_near; i++) {
        Ent *e = near_list[i];
        unsigned char t = e->type;
        const signed char *hb;
        int ex = e->px, ey = e->py, y1;
        if (t == ET_ROPE) {
            if (e->state || b_px + 15 < ex + 6 || b_px > ex + 9 ||
                b_py + 11 < ey || b_py + 4 > ey + 15) continue;
            e->state = 1; e->tm = 0; e->t = 0;       /* the rope snaps */
            rope_start();
            b_hit = 1; b_timer = 0;
            sfx_play(SND_SHOTHIT);
            return;
        }
        if (t >= ET_CANNON0 || e->state >= ST_DYING) continue;
        hb = hbox[t];
        y1 = ey + hb[1];
        if (b_px + 15 >= ex + hb[0] && b_px <= ex + hb[2] &&
            b_py + 11 >= y1 && b_py + 4 <= ey + hb[3]) {
            b_hit = 1; b_timer = 0;
            sfx_play(SND_SHOTHIT);
            ent_shot(e);
            return;
        }
    }
    /* enemy rays: both are destroyed */
    for (i = 0; i < N_PROJ; i++)
        if (pj_on[i] == PJ_RAY &&
            b_px + 15 >= pj_x[i] && b_px <= pj_x[i] + 15 &&
            b_py + 11 >= pj_y[i] && b_py + 4 <= pj_y[i] + 7) {
            pj_on[i] = PJ_ZAP; pj_t[i] = 10;
            b_hit = 1; b_timer = 0;
            return;
        }
}

/* ----------------------------------------------------------------- random -- */
/* CG calls std::rand(); the AI only needs a decent 16-bit source. */
static unsigned int rs = 0xACE1;
static unsigned int rnd16(void) {
    rs ^= rs << 7; rs ^= rs >> 9; rs ^= rs << 8;
    return rs;
}
/* getProbability(n): rand()%1000 <= n  ->  rnd16 below (n+1)*65.536 */
#define PROB(n)  (rnd16() < (unsigned int)(((n) + 1) * 65))

/* ----------------------------------------------------------------- enemies - */
/* A state-for-state port of Commander Genius' vorticon AI (CYorp, CGarg,
   CVorticon, CButler, CTank, CIceCannon, CRope).  CG velocities are in
   1/32 px per tick; this engine uses 8.8, hence EV().  Each tick runs
   CG's order: collision flags, falling, the nearby/touch hooks, the AI,
   then inertia and friction in X. */

static signed char hbx[4];                /* x1,y1,x2,y2 of the current entity's hit box */
#define hx1 hbx[0]
#define hy1 hbx[1]
#define hx2 hbx[2]
#define hy2 hbx[3]
static unsigned char bvalid, bfl;         /* lazily computed blocked flags */
static unsigned char cliff_dir;           /* butler/tank: facing, else CENTER_ */

/* does the floor end under the front of the box (CG turnAroundOnCliff)? */
static unsigned char ent_cliff(void) {
    int fy = E.py + hy2 + 1;
    unsigned char fl = mflag_a(E.px + hx1 - 1, fy) & F_UP;
    unsigned char fr = mflag_a(E.px + hx2 + 1, fy) & F_UP;
    if (cliff_dir == LEFT_)  return !fl && fr;
    return !fr && fl;
}

static unsigned char ent_bl(void) {
    if (!(bvalid & 1)) {
        int x = E.px + hx1 - 1, y1 = E.py + hy1, y2 = E.py + hy2;
        unsigned char f = mflag_a(x, y1) | mflag_a(x, y2);
        if (y2 - y1 > 16) f |= mflag_a(x, (y1 + y2) >> 1);
        bvalid |= 1;
        if ((f & F_BLKL) || (cliff_dir == LEFT_ && ent_cliff())) bfl |= 1;
        else bfl &= ~1;
    }
    return bfl & 1;
}
static unsigned char ent_br(void) {
    if (!(bvalid & 2)) {
        int x = E.px + hx2 + 1, y1 = E.py + hy1, y2 = E.py + hy2;
        unsigned char f = mflag_a(x, y1) | mflag_a(x, y2);
        if (y2 - y1 > 16) f |= mflag_a(x, (y1 + y2) >> 1);
        bvalid |= 2;
        if ((f & F_BLKR) || (cliff_dir == RIGHT_ && ent_cliff())) bfl |= 2;
        else bfl &= ~2;
    }
    return (bfl & 2) != 0;
}
static unsigned char ent_bu(void) {
    if (!(bvalid & 4)) {
        int y = E.py + hy1 - 1;
        bvalid |= 4;
        if ((mflag_a(E.px + hx1, y) | mflag_a(E.px + hx2, y)) & F_CEIL) bfl |= 4;
        else bfl &= ~4;
    }
    return (bfl & 4) != 0;
}
static unsigned char ent_bd(void) {
    if (!(bvalid & 8)) {
        int y = E.py + hy2 + 1;
        bvalid |= 8;
        if ((mflag_a(E.px + hx1, y) | mflag_a(E.px + hx2, y)) & F_UP) bfl |= 8;
        else bfl &= ~8;
    }
    return (bfl & 8) != 0;
}
#define BL()  ent_bl()
#define BR()  ent_br()
#define BU()  ent_bu()
#define BD()  ent_bd()

/* CG processFallPhysics: gravity 4 units/tick, terminal 104 */
static void ent_fall(void) {
    int acc, d;
    unsigned char bd = BD(), bu = 0;
    if (bd) E.fl &= ~FL_AIR; else E.fl |= FL_AIR;
    if (E.vy < 0) bu = BU();
    if (E.vy < 0 && !bu) {
        acc = E.sy + E.vy;
        d = acc >> 8; E.sy = acc - (d << 8);        /* d <= 0 */
        if (d < 0) {
            int ny = E.py + hy1 + d;
            if ((mflag_a(E.px + hx1, ny) | mflag_a(E.px + hx2, ny)) & F_CEIL) {
                E.vy = 0; E.sy = 0;
            } else E.py += d;
            bvalid &= ~(4 | 8);
        }
        E.vy += EV(4);
    } else if (E.vy >= 0 && !bd) {
        acc = E.sy + E.vy;
        d = acc >> 8; E.sy = acc - (d << 8);
        if (d > 0) {
            int fy = E.py + hy2, ny = fy + d;
            unsigned char f = mflag_a(E.px + hx1, ny) | mflag_a(E.px + hx2, ny);
            if ((f & F_UP) && ((f & F_CEIL) || (fy >> 4) < (ny >> 4))) {
                E.py = (ny & ~15) - hy2 - 1; E.vy = 0; E.sy = 0;
            } else E.py += d;
            bvalid &= ~(4 | 8);
        }
        if (E.vy > EV(104)) E.vy = EV(104);
        else if (E.vy < EV(104)) E.vy += EV(4);
    }
    if ((bd && E.vy > 0) || (bu && E.vy < 0)) E.vy = 0;
}

/* CG InertiaAndFriction_X: move by xinertia unless blocked, then friction 10 */
static void ent_inertia(void) {
    int acc, d;
    if ((E.vx > 0 && !BR()) || (E.vx < 0 && !BL())) {
        acc = E.sx + E.vx;
        d = acc >> 8; E.sx = acc - (d << 8);
        if (d > 0) {
            int fx = E.px + hx2 + d;
            if (((unsigned int)fx >> 4) != ((unsigned int)(E.px + hx2) >> 4)) {
                int y1 = E.py + hy1, y2 = E.py + hy2;
                unsigned char f = mflag_a(fx, y1) | mflag_a(fx, y2);
                if (y2 - y1 > 16) f |= mflag_a(fx, (y1 + y2) >> 1);
                if (f & F_BLKR) { E.px = (fx & ~15) - hx2 - 1; E.vx = 0; d = 0; }
            }
            E.px += d;
        } else if (d < 0) {
            int fx = E.px + hx1 + d;
            if (((unsigned int)fx >> 4) != ((unsigned int)(E.px + hx1) >> 4)) {
                int y1 = E.py + hy1, y2 = E.py + hy2;
                unsigned char f = mflag_a(fx, y1) | mflag_a(fx, y2);
                if (y2 - y1 > 16) f |= mflag_a(fx, (y1 + y2) >> 1);
                if (f & F_BLKL) { E.px = ((fx >> 4) + 1) * 16 - hx1; E.vx = 0; d = 0; }
            }
            E.px += d;
        }
        bvalid &= ~3;
    } else E.vx = 0;
    if (E.vx > 0)      { E.vx -= EV(10); if (E.vx < 0) E.vx = 0; }
    else if (E.vx < 0) { E.vx += EV(10); if (E.vx > 0) E.vx = 0; }
}

/* Keen's box (CG: x 4..11, y 1..23) against E's hit box */
static unsigned char hit_keen(void) {
    return !dying &&
           px + 4 <= E.px + hx2 && px + 11 >= E.px + hx1 &&
           py + 1 <= E.py + hy2 && py + 23 >= E.py + hy1;
}

/* CG CPlayer::bump: shove Keen at bumpamount (160 units) */
static void keen_bump(unsigned char right) {
    if (jprep) return;
    sfx_play(SND_YORPBUMP);
    dir_right = right;
    vx = right ? EV(160) : -EV(160);
}


/* ---- yorp ------------------------------------------------------------- */

static void yorp_ai(unsigned char seen) {
    /* isNearby: leave the look state once it has looked around enough */
    if (E.state == Y_LOOK && E.lk > 12 && E.tm == 12) {
        E.dir = (px < E.px) ? LEFT_ : RIGHT_;
        if (BL()) E.dir = RIGHT_;
        if (BR()) E.dir = LEFT_;
        E.tm = 0; E.wf = 0; E.t = 0;
        E.state = Y_MOVE;
    }
    /* getTouchedBy */
    if (E.state < ST_DYING && E.state != Y_STUN && hit_keen()) {
        if (py + 23 < E.py + hy2 - 16) {
            sfx_play(SND_YORPBOP);
            E.state = Y_STUN; E.lk = 0; E.tm = 0; E.an = 0;
            pogoing = 0;
            player_launch(EV(-7));                 /* "a bit of resistance" */
        } else keen_bump(E.dir);
    }
    if (!seen) return;
    if (E.hp == 0 && E.state != ST_DYING) {
        E.state = ST_DYING; E.tm = 0;
        E.frame = YF_DYING; E.vy = EV(-40);
        sfx_play(SND_YORPSCREAM);
    }
    switch (E.state) {
    case Y_LOOK:
        if (!E.tm) {
            E.lk++;
            switch (E.an) {
            case 0: E.frame = YF_LOOK_L; break;
            case 1: E.frame = YF_STAND;  break;
            case 2: E.frame = YF_LOOK_R; break;
            default: E.frame = YF_STAND; break;
            }
            E.lk++;
            if (++E.an > 3) E.an = 0;
            E.tm = 16;
        } else E.tm--;
        break;
    case Y_MOVE:
        if (E.t > 50 && BD() && (rnd16() % 30) == 1) {
            E.lk = 0; E.tm = 0; E.an = 1; E.state = Y_LOOK;
            break;
        }
        if (BD() && PROB(8)) {
            E.vy = EV(-40 - (int)(rnd16() % 3));
            bvalid &= ~8; bfl &= ~8;               /* blockedd = false */
        }
        if (E.dir == LEFT_) {
            E.frame = YF_WALK_LEFT + E.wf;
            if (!BL()) { E.vx = -EV(19); E.t++; }
            else { E.lk = 4; E.tm = 0; E.an = 1; E.state = Y_LOOK; }
        } else {
            E.frame = YF_WALK_RIGHT + E.wf;
            if (!BR()) { E.vx = EV(19); E.t++; }
            else { E.lk = 4; E.tm = 0; E.an = 1; E.state = Y_LOOK; }
        }
        if (E.tm > 5) { E.wf ^= 1; E.tm = 0; } else E.tm++;
        break;
    case Y_STUN:
        E.frame = YF_STUN + E.wf;
        if (E.tm > 5) {
            if (E.lk > 24) { E.lk = 0; E.tm = 0; E.an = 1; E.state = Y_LOOK; }
            else E.lk++;
            E.wf ^= 1; E.tm = 0;
        } else E.tm++;
        break;
    case ST_DYING:
        if (E.vy >= 0 && BD()) { E.frame = YF_DEAD; E.state = ST_DEAD; }
        break;
    }
}

/* ---- garg ------------------------------------------------------------- */

static void garg_charge(void) {
    if (E.dir == LEFT_) {
        E.frame = GF_WALK_LEFT + E.wf;
        if (!BL()) { E.vx = -EV(68); E.t++; }
        else { E.lk = 0; E.tm = 0; E.state = G_MOVE; }
    } else {
        E.frame = GF_WALK_RIGHT + E.wf;
        if (!BR()) { E.vx = EV(68); E.t++; }
        else { E.lk = 0; E.tm = 0; E.state = G_MOVE; }
    }
    if (E.tm > 7) { E.wf ^= 1; E.tm = 0; } else E.tm++;
}

static void garg_ai(void) {
    /* isNearby */
    if (E.state == G_LOOK) {
        if (E.lk > 3) E.dir = (px < E.px) ? LEFT_ : RIGHT_;
    } else if (E.state == G_MOVE) {
        int kb = py + 23, gb = E.py + hy2;
        if (kb >= gb - 16 && kb <= gb + 16) {
            if (++E.ks > 50) {
                E.ks = 0; E.tm = 0; E.lk = 1;
                E.fl |= FL_ABOUT; E.state = G_LOOK;
            }
        } else E.ks = 0;
    }
    /* getTouchedBy */
    if (E.state < ST_DYING && hit_keen()) player_die();
    if (E.hp == 0 && E.state != ST_DYING) {
        E.state = ST_DYING; E.frame = GF_DYING; E.vy = EV(-40);
        sfx_play(SND_GARGSCREAM);
    }
    if (E.state != G_LOOK && E.state != G_CHARGE && E.state != ST_DYING) {
        E.frame = ((E.dir == LEFT_) ? GF_WALK_LEFT : GF_WALK_RIGHT) + E.wf;
        if (E.dir == LEFT_ && BL()) E.dir = RIGHT_;
        else if (E.dir == RIGHT_ && BR()) E.dir = LEFT_;
        E.vx = (E.dir == LEFT_) ? -EV(19) : EV(19);
        E.t++;
    }
    switch (E.state) {
    case ST_DYING:
        if (BD() && E.vy >= 0) { E.frame = GF_DEAD; E.state = ST_DEAD; }
        break;
    case G_LOOK:
        if (E.lk > 3) {
            if (!(E.fl & FL_ABOUT) && rnd16() % 3 == 1)
                E.dir = (E.dir == LEFT_) ? RIGHT_ : LEFT_;
            if (BL()) E.dir = RIGHT_;
            else if (BR()) E.dir = LEFT_;
            E.tm = 0; E.wf = 0; E.t = 0;
            if (E.fl & FL_ABOUT) { E.state = G_CHARGE; E.fl &= ~FL_ABOUT; }
            else E.state = G_MOVE;
        }
        if (!E.tm) {
            E.frame = GF_STAND + E.an;
            E.lk++;
            if (++E.an > 3) E.an = 0;
            E.tm = 17;
        } else E.tm--;
        break;
    case G_MOVE:
        if (E.t > 100 && rnd16() % 100 == 50) {
            E.lk = 0; E.tm = 0; E.state = G_LOOK;
            break;
        }
        if (E.tm > 5) { E.wf ^= 1; E.tm = 0; } else E.tm++;
        break;
    case G_JUMP:
        if (E.jt) E.jt--; else E.state = G_CHARGE;
        if (BD()) E.state = G_CHARGE;
        garg_charge();
        break;
    case G_CHARGE:
        garg_charge();
        if (!BD()) { E.state = G_JUMP; E.vy = EV(-96); E.jt = 160; }
        break;
    }
}

/* ---- vorticon ----------------------------------------------------------- */

static void vort_jump(void) {
    if (!BD()) return;
    E.wf = 0; E.an = 0;
    E.vy = -(int)((rnd16() % 60) + 90) * 8;
    E.frame = (E.dir == RIGHT_) ? VF_JUMP_R : VF_JUMP_L;
    E.state = V_JUMP;
}

static void vort_ai(void) {
    unsigned char again;
    /* isNearby: the look state also ticks here, so it runs twice a tick */
    if (E.state == V_LOOK) {
        E.frame = VF_LOOK + E.wf;
        if (E.an > 15) {
            if (E.wf > 0) {
                int dxx = px - E.px;
                unsigned char isleft = dxx < 0;
                if (isleft) dxx = -dxx;
                if (dxx <= 128) {
                    if (isleft && !BL()) E.dir = LEFT_;
                    else if (!BR()) E.dir = RIGHT_;
                }
                E.tm = 0; E.wf = 0; E.state = V_WALK;
            } else E.wf++;
            E.an = 0;
        } else E.an++;
    }
    /* getTouchedBy */
    if (E.state < ST_DYING && hit_keen()) player_die();
    if (E.hp == 0 && E.state != ST_DYING) {
        E.an = 0; E.wf = 0; E.state = ST_DYING;
        flash_t = 14;
        sfx_play(SND_VORTSCREAM);
    }
    do {
        again = 0;
        switch (E.state) {
        case V_JUMP:
            if (E.dir == RIGHT_ && !BR()) E.vx = EV(28);
            else if (!BL()) E.vx = -EV(28);
            if (E.vy == 0 && BD()) { E.state = V_LOOK; again = 1; }
            break;
        case V_LOOK:
            E.frame = VF_LOOK + E.wf;
            if (E.an > 15) {
                if (E.wf > 0) {
                    if (BL()) E.dir = RIGHT_;
                    else if (BR()) E.dir = LEFT_;
                    E.tm = 0; E.wf = 0; E.state = V_WALK;
                } else E.wf++;
                E.an = 0;
            } else E.an++;
            break;
        case V_WALK:
            E.t++;
            if (PROB(20) && !BU()) {
                vort_jump();
                if (E.state == V_JUMP) { again = 1; break; }
                again = 1; break;
            }
            if (E.dir == LEFT_) {
                E.frame = VF_WALK_LEFT + E.wf;
                if (!BL()) E.vx = -EV(28);
                else {
                    E.wf = 0; E.an = 0; E.state = V_LOOK;
                    if (E.t < 160 && BD() && !BU()) {
                        vort_jump();
                        E.vy = (rnd16() & 1) ? EV(-150) : EV(-90);
                        again = 1;
                    } else E.t = 0;
                }
            } else {
                E.frame = VF_WALK_RIGHT + E.wf;
                if (!BR()) E.vx = EV(28);
                else {
                    E.wf = 0; E.an = 0; E.state = V_LOOK;
                    if (E.t < 160 && BD() && !BU()) {
                        vort_jump();
                        E.vy = (rnd16() & 1) ? EV(-150) : EV(-90);
                        again = 1;
                    } else E.t = 0;
                }
            }
            if (E.state == V_WALK) {
                if (E.an > 4) { E.wf = (E.wf >= 3) ? 0 : E.wf + 1; E.an = 0; }
                else E.an++;
            }
            break;
        case ST_DYING:
            E.frame = VF_DYING + E.wf;
            if (E.an > 25) {
                E.wf++;
                if (E.wf >= 6) { E.wf = 5; E.frame = VF_DYING + 5; E.state = ST_DEAD; }
                E.an = 0;
            } else E.an++;
            break;
        }
    } while (again);
}

/* ---- butler ------------------------------------------------------------- */

static void butler_ai(void) {
    /* getTouchedBy: shove Keen the way the butler faces */
    if (hit_keen()) keen_bump(E.dir != LEFT_);
    switch (E.state) {
    case B_TURN:
        if (E.tm > 10) {
            E.dir = (E.dir == LEFT_) ? RIGHT_ : LEFT_;
            E.an = 0; E.state = B_WALK;
        } else E.tm++;
        break;
    case B_WALK:
        if (E.dir == LEFT_) {
            E.frame = BF_WALK_LEFT + E.wf;
            if (!BL()) E.vx = -EV(28);
            else { E.frame = BF_TURN_RIGHT; E.wf = 0; E.tm = 0; E.an = 0; E.state = B_TURN; }
        } else {
            E.frame = BF_WALK_RIGHT + E.wf;
            if (!BR()) E.vx = EV(28);
            else { E.frame = BF_TURN_LEFT; E.wf = 0; E.tm = 0; E.an = 0; E.state = B_TURN; }
        }
        if (E.an > 6) { E.wf = (E.wf >= 3) ? 0 : E.wf + 1; E.an = 0; }
        else E.an++;
        break;
    case B_FALL:
        if (BD()) { E.state = B_WALK; E.dir = RIGHT_; }
        break;
    }
}

/* ---- tank ---------------------------------------------------------------- */

static void tank_ai(void) {
    if (hit_keen()) keen_bump(E.dir);
    /* isNearby */
    if (E.state == T_LOOK && E.tm > 25) {
        int kmid = px + 7, tmid = E.px + 7;
        if (kmid < tmid)      { E.dir = LEFT_;  E.frame = TF_WALK_LEFT; }
        else if (kmid > tmid) { E.dir = RIGHT_; E.frame = TF_WALK_RIGHT; }
        E.state = T_WALK; E.an = 0; E.tm = 0;
    }
    switch (E.state) {
    case T_WALK:
        if (E.dir == LEFT_) {
            E.jt = LEFT_;
            E.frame = TF_WALK_LEFT + E.wf;
            E.vx = -EV(28);
            if (BL()) { E.dir = RIGHT_; E.wf = 0; E.tm = 0; E.an = 0; E.state = T_TURN; }
        } else {
            E.jt = RIGHT_;
            E.frame = TF_WALK_RIGHT + E.wf;
            E.vx = EV(28);
            if (BR()) { E.dir = LEFT_; E.wf = 0; E.tm = 0; E.an = 0; E.state = T_TURN; }
        }
        E.t--;
        if (E.an > 6) { E.wf = (E.wf >= 3) ? 0 : E.wf + 1; E.an = 0; }
        else E.an++;
        if (E.t == 0) { E.wf = 0; E.tm = 0; E.an = 0; E.state = T_WAIT; }
        break;
    case T_WAIT:
        if (E.tm > 22) {
            /* fire: the ray starts 5 px below the top, at the tank's middle */
            if (E.dir == RIGHT_)
                spawn_ray(E.px + 7, E.py + 5, 1);
            else
                spawn_ray(E.px + 7 - 16, E.py + 5, 0);
            E.state = T_WAITLOOK; E.wf = 0; E.tm = 0; E.an = 0;
            E.t = 20 + ((rnd16() >> 8) % 10) * 18;
        } else E.tm++;
        break;
    case T_TURN:
        E.frame = TF_LOOK + E.wf;
        if (E.an > 4) { E.wf ^= 1; E.an = 0; } else E.an++;
        if (E.tm > 25) { E.state = T_WALK; E.an = 0; E.tm = 0; }
        else E.tm++;
        break;
    case T_WAITLOOK:
        if (E.tm > 14) { E.tm = 0; E.state = T_LOOK; } else E.tm++;
        break;
    case T_LOOK:
        E.frame = TF_LOOK + E.wf;
        if (E.an > 4) { E.wf ^= 1; E.an = 0; } else E.an++;
        E.tm++;
        break;
    }
}

/* ---- projectiles --------------------------------------------------------- */
static void spawn_proj(unsigned char kind, int x, int y, int vx8, int vy8) {
    unsigned char i;
    for (i = 0; i < N_PROJ; i++)
        if (!pj_on[i]) {
            pj_on[i] = kind;
            pj_x[i] = x; pj_y[i] = y; pj_vx[i] = vx8; pj_vy[i] = vy8;
            pj_sx[i] = pj_sy[i] = 0; pj_t[i] = 0;
            return;
        }
}
static void spawn_ray(int x, int y, unsigned char right) {
    if (on_screen_pt(x, y)) sfx_play(SND_TANKFIRE);
    spawn_proj(PJ_RAY, x, y, right ? EV(108) : -EV(108), 0);
}

/* CG CPlayer::freeze: full-height kick, 250 ticks in the ice cube */
static void keen_freeze(signed char dirx) {
    if (exiting || dying || perf_on == 2) return;
    pogoing = 0; jprep = 0;
    player_launch(JUMP_V);
    stun_t = 250;
    if (dirx) {
        dir_right = dirx > 0;
        vx = dirx > 0 ? WALK_MAX : -WALK_MAX;
    } else if (vx < EV(16))
        vx = (rnd16() & 1) ? EV(16) : -EV(16);
}

static void ice_smash(int x, int y) {
    unsigned char i, n = 0;
    int dxx = px - x, dyy = py - y;
    if (!on_screen_pt(x, y)) return;
    if (dxx < 0) dxx = -dxx;
    if (dyy < 0) dyy = -dyy;
    if (dxx < 320 && dyy < 320) sfx_play(SND_CHUNKSMASH);   /* CG SILENT_DIST */
    for (i = 0; i < N_BITS && n < 4; i++)
        if (!bit_t[i]) {
            bit_x[i] = x + 4; bit_y[i] = y + 4;
            bit_dx[i] = (n & 1) ? 1 : -1;
            bit_dy[i] = (n & 2) ? 1 : -1;
            bit_t[i] = 25; n++;
        }
}

static void proj_update(void) {
    unsigned char i;
    for (i = 0; i < N_PROJ; i++) {
        int acc, d, nx, ny;
        unsigned char k = pj_on[i];
        if (!k) continue;
        if (k == PJ_ZAP) {                           /* a ray's impact flash */
            if (!pj_t[i]--) pj_on[i] = 0;
            continue;
        }
        acc = pj_sx[i] + pj_vx[i];
        d = acc >> 8; pj_sx[i] = acc - (d << 8);
        nx = pj_x[i] + d;
        acc = pj_sy[i] + pj_vy[i];
        d = acc >> 8; pj_sy[i] = acc - (d << 8);
        ny = pj_y[i] + d;
        pj_x[i] = nx; pj_y[i] = ny;
        if (nx < (int)cam_x - 512 || nx > (int)cam_x + 768 ||
            ny < (int)cam_y - 400 || ny > (int)cam_y + 592) { pj_on[i] = 0; continue; }
        if (k == PJ_CHUNK) {
            int vxp = pj_vx[i], vyp = pj_vy[i];
            unsigned char f = 0, hit = 0;
            /* CG: smash on the side it flies toward (box 4..14) */
            if (vxp > 0)      f |= (mflag_a(nx + 15, ny + 4) | mflag_a(nx + 15, ny + 14)) & F_BLKR;
            else if (vxp < 0) f |= (mflag_a(nx + 3, ny + 4) | mflag_a(nx + 3, ny + 14)) & F_BLKL;
            if (vyp > 0)      f |= (mflag_a(nx + 4, ny + 15) | mflag_a(nx + 14, ny + 15)) & F_UP;
            else if (vyp < 0) f |= (mflag_a(nx + 4, ny + 3) | mflag_a(nx + 14, ny + 3)) & F_CEIL;
            if (f) { ice_smash(nx, ny); pj_on[i] = 0; continue; }
            if (!dying && nx + 14 >= px + 4 && nx + 4 <= px + 11 &&
                ny + 14 >= py + 1 && ny + 4 <= py + 23) {
                /* frozen: slide the way the chunk flew */
                keen_freeze(vxp > 0 ? 1 : (vxp < 0 ? -1 : 0));
                sfx_play(SND_KEENCICLE);
                hit = 1;
            }
            if (hit) { ice_smash(nx, ny); pj_on[i] = 0; }
            continue;
        }
        /* enemy ray (CG CRay): zaps on walls, kills Keen (thaws him when
           he is frozen), and hurts any other enemy it crosses */
        {
            unsigned char right = pj_vx[i] > 0, j;
            if (mflag_a(right ? nx + 15 : nx, ny + 4) & (right ? F_BLKR : F_BLKL)) {
                pj_on[i] = PJ_ZAP; pj_t[i] = 10;
                continue;
            }
            if (!dying && nx + 15 >= px + 4 && nx <= px + 11 &&
                ny + 7 >= py + 1 && ny <= py + 23) {
                if (stun_t) { if (stun_t > 20) stun_t = 20; }
                else player_die();
                pj_on[i] = PJ_ZAP; pj_t[i] = 10;
                continue;
            }
            for (j = 0; j < n_near; j++) {
                Ent *e = near_list[j];
                const signed char *hb;
                if (e->type >= ET_CANNON0 || e->type == ET_TANK ||
                    e->state >= ST_DYING) continue;
                hb = hbox[e->type];
                if (nx + 15 >= e->px + hb[0] && nx <= e->px + hb[2] &&
                    ny + 7 >= e->py + hb[1] && ny <= e->py + hb[3]) {
                    ent_shot(e);
                    pj_on[i] = PJ_ZAP; pj_t[i] = 10;
                    break;
                }
            }
        }
    }
    /* ice splinters: 25 ticks, 2.5 px per tick on both axes */
    for (i = 0; i < N_BITS; i++)
        if (bit_t[i]) {
            unsigned char s = 2 + (bit_t[i] & 1);
            bit_x[i] += bit_dx[i] * s;
            bit_y[i] += bit_dy[i] * s;
            bit_t[i]--;
        }
}

/* ---- ice cannons --------------------------------------------------------- */
static void cannon_ai(Ent *ep) {
    static const signed char cvx[4] = { 1, 0, 0, -1 };
    static const signed char cvy[4] = { -1, -1, 1, -1 };
    if (ep->tm >= 64) {
        unsigned char v = ep->type - ET_CANNON0;
        int x = ep->px, dxx = px - ep->px, dyy = py - ep->py;
        ep->tm = 0;
        if (dxx < 0) dxx = -dxx;
        if (dyy < 0) dyy = -dyy;
        if (cvx[v] > 0) x += 16;
        if (dyy < 320 && dxx < 320) sfx_play(SND_CANNON);   /* SILENT_DIST: 20 tiles */
        spawn_proj(PJ_CHUNK, x, ep->py,
                   cvx[v] * (cvx[v] && cvy[v] ? EV(60) : EV(80)),
                   cvy[v] * (cvx[v] && cvy[v] ? EV(60) : EV(80)));
    } else ep->tm++;
}

/* ---- the level-16 rope and its stone (CG CRope) ------------------------ */

static void rope_start(void) {
    rope_sx = (rope_ent->px >> 4) - 4;
    rope_sy = (rope_ent->py >> 4) + 1;
}

static void rope_cell(unsigned int mx, unsigned int my, unsigned char mt) {
    rope_map[my - (rope_ent->py >> 4) - 1][mx] = mt;
    nt_update_cell_a(mx, my);
}
static unsigned char rope_get(unsigned int mx, unsigned int my) {
    return rope_map[my - (rope_ent->py >> 4) - 1][mx];
}

/* kill whatever the falling stone lands on (CG kill_intersecting_tile) */
static void rope_crush(void) {
    unsigned int x0 = rope_sx << 4, y0 = (rope_sy << 4) + 8;
    unsigned char j;
    int kx = px + 7, ky = py + 1;
    /* Keen */
    if (!dying && px + 11 >= (int)x0 && px + 4 < (int)(x0 + 144) &&
        py + 23 >= (int)y0 && py + 1 < (int)(y0 + 24)) {
        unsigned int cy = (rope_sy + 1) << 4;
        if (kx >= (int)x0 - 16 && kx <= (int)(x0 + 144) &&
            ky >= (int)cy && ky <= (int)cy + 32)
            player_die();
    }
    for (j = 0; j < y_n; j++) {
        Ent *e = &ents[j];
        const signed char *hb;
        unsigned int cy;
        if (e->type >= ET_CANNON0 || e->state >= ST_DEAD) continue;
        hb = hbox[e->type];
        if (e->px + hb[2] < (int)x0 || e->px + hb[0] >= (int)(x0 + 144) ||
            e->py + hb[3] < (int)y0 || e->py + hb[1] >= (int)(y0 + 24)) continue;
        cy = (rope_sy + 1) << 4;
        if (e->py + hb[1] >= (int)cy && e->py + hb[1] <= (int)cy + 32) {
            e->state = ST_GONE;
            if (e->slot != 0xFF) { slot_owner[e->slot] = 0; e->slot = 0xFF; }
        }
    }
}

static void rope_tick(Ent *ep) {
    if (ep->state != 1) return;
    rope_crush();
    if (ep->tm) { ep->tm--; return; }
    ep->tm = 6;
    {
        unsigned char x, y;
        /* move the stone down one row: rows ya+2 <- ya+1 <- ya <- background */
        for (y = STONE_H; y > 0; y--)
            for (x = 0; x < STONE_W; x++)
                rope_cell(rope_sx + x, rope_sy + y, rope_get(rope_sx + x, rope_sy + y - 1));
        for (x = 0; x < STONE_W; x++) rope_cell(rope_sx + x, rope_sy, rope_bg);
    }
    rope_sy++;
    shake_t = 8;
    if (++ep->t >= 4) ep->state = 2;                 /* landed: rope is gone */
}

/* Rebuild the near-entity list: everything within 128px of the screen
   horizontally / 144px vertically (CG's visibility window is 48px, so the
   list is a superset). Rebuilt every 8 frames or when the camera drifted
   15px; no entity moves 48px in 8 ticks. */
static unsigned int near_cx, near_cy;
static void rebuild_near(void) {
    unsigned char i, n = 0;
    unsigned int bx = cam_x - 128, by = cam_y - 144;
    Ent *e = ents;
    near_cx = cam_x; near_cy = cam_y;
    for (i = 0; i < y_n; i++, e++) {
        if ((unsigned int)(e->px - bx) <= 496 && (unsigned int)(e->py - by) <= 480) {
            near_list[n++] = e;
        } else if (e->slot != 0xFF) {       /* far away: release its sprite slot */
            slot_owner[e->slot] = 0; e->slot = 0xFF;
        }
    }
    n_near = n;
}

static void ent_update(Ent *ep) {
    unsigned char t = ep->type, seen = ep->fl & FL_SEEN;
    unsigned int ux, uy;

    ent_rel(ep);
    /* what build_sprites must look at: anything within 16px of the screen;
       release the sprite slot of anything farther out */
    if ((unsigned int)(er_x + 32) <= 303 && (unsigned int)(er_y + 48) <= 255) {
        if (t < ET_CANNON0 && ep->state < ST_GONE) vis_list[n_vis++] = ep;
    } else if (ep->slot != 0xFF) {
        slot_owner[ep->slot] = 0; ep->slot = 0xFF;
    }
    if (ep->state >= ST_DEAD && t < ET_CANNON0) return;   /* corpse / gone */
    /* CG checkforScenario: only objects inside the visibility window (screen
       + 3 tiles) think, plus anything still in mid-air */
    ux = (unsigned int)er_x + 48; uy = (unsigned int)er_y + 48;
    if ((ux > 352 || uy > 288) && !(ep->fl & FL_AIR)) return;
    if (t >= ET_CANNON0) {
        if (t == ET_ROPE) rope_tick(ep); else cannon_ai(ep);
        return;
    }
    if (!seen && t != ET_YORP) {                 /* wait until first drawn */
        if ((unsigned int)(er_x + 24) < 280 && (unsigned int)(er_y + 32) < 224)
            ep->fl |= FL_SEEN;
        return;
    }

    memcpy(&E, ep, ENT_DIRTY);
    E.hp = ep->hp; E.type = t;
    memcpy(hbx, hbox[t], 4);
    bvalid = 0; bfl = 0;
    cliff_dir = (t == ET_BUTLER) ? E.dir : ((t == ET_TANK) ? E.jt : CENTER_);

    ent_fall();
    switch (t) {
    case ET_YORP:   yorp_ai(seen); break;
    case ET_GARG:   garg_ai(); break;
    case ET_VORT:   vort_ai(); break;
    case ET_BUTLER: butler_ai(); break;
    case ET_TANK:   tank_ai(); break;
    }
    ent_inertia();
    if ((unsigned int)(er_x + 24) < 280 && (unsigned int)(er_y + 32) < 224)
        E.fl |= FL_SEEN;
    memcpy(ep, &E, ENT_DIRTY);
}

/* one game tick of everything that is not Keen (called through ai_run) */
void ai_tick(void) {
    unsigned char k;
    bullet_update();
    proj_update();
    frame_ct++; perf_loops++;
    if (flash_t) flash_t--;
    if (shake_t) shake_t--;
    if (++near_timer >= 8 || (unsigned int)(cam_x - near_cx + 15) > 30 ||
        (unsigned int)(cam_y - near_cy + 15) > 30) {
        near_timer = 0;
        rebuild_near();
    }
    { Ent **np = near_list; n_vis = 0; for (k = n_near; k; k--) ent_update(*np++); }
}
