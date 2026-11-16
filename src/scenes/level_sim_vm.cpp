#include "level_sim.hpp"
#include "bullet_catalog.hpp"
#include "../log.hpp"
#include "../numeric/numeric.hpp"
#include <algorithm>
#include <cmath>

namespace th95::scene {

float LevelSimulation::variable(Enemy &enemy, Thread &thread, int id)
{
    if (id >= 10000 && id <= 10007) return float(thread.integers[id - 10000]);
    if (id >= 10008 && id <= 10015) return thread.floats[id - 10008];
    if (id >= 10020 && id <= 10023) return float(thread.counters[id - 10020]);
    if (id >= 10036 && id <= 10039) return float(thread.intArguments[id - 10036]);
    if (id >= 10040 && id <= 10043) return thread.arguments[id - 10040];
    if (id >= 10044 && id <= 10047) return float(m_globalIntegers[id - 10044]);
    if (id >= 10048 && id <= 10051) return m_globals[id - 10048];
    if (id >= 10077 && id <= 10080) return thread.scratch[id - 10077];
    switch (id) {
    case 10016: return float(random() & 0x7fffffff);
    case 10017: return unit();
    case 10018: return float(random());
    case 10019: return numeric::sub(numeric::mul(unit(), 2.0f), 1.0f);
    case 10026: return enemy.x;
    case 10027: return enemy.y;
    case 10028: return 0;
    case 10029: return playerX;
    case 10030: return playerY;
    case 10031: return 0;
    case 10032: return numeric::atan2(numeric::sub(playerY, enemy.y), numeric::sub(playerX, enemy.x));
    case 10033: return float(enemy.photoCount);
    case 10052: return enemy.angle;
    case 10053: return enemy.angularVelocity;
    case 10054: return enemy.speed;
    case 10055: return enemy.acceleration;
    case 10065: return numeric::sub(numeric::mul(numeric::mul(unit(), 2.0f), float(numeric::kPi)),
                                   float(numeric::kPi));
    case 10069: return enemy.vx;
    case 10070: return enemy.vy;
    case 10081: return float(photos);
    case 10085:
        return numeric::atan2(
            numeric::sub(playerY, numeric::add(enemy.y, enemy.shotY)),
            numeric::sub(playerX, numeric::add(enemy.x, enemy.shotX)));
    default:
        recordFault(vm::Fault::UnsupportedVariable, 0, 0, id);
        return 0;
    }
}

void LevelSimulation::assign(Enemy &enemy, Thread &thread, int id, float value, bool integer)
{
    const auto stored = [&]() { return integer ? float(numeric::truncToInt(value)) : value; };
    if (id >= 10000 && id <= 10007) {
        thread.integers[id - 10000] = numeric::truncToInt(value);
        return;
    }
    if (id >= 10008 && id <= 10015) {
        thread.floats[id - 10008] = value;
        return;
    }
    if (id >= 10020 && id <= 10023) {
        thread.counters[id - 10020] = numeric::truncToInt(value);
        return;
    }
    if (id >= 10036 && id <= 10039) {
        thread.intArguments[id - 10036] = numeric::truncToInt(value);
        return;
    }
    if (id >= 10040 && id <= 10043) {
        thread.arguments[id - 10040] = value;
        return;
    }
    if (id >= 10044 && id <= 10047) {
        m_globalIntegers[id - 10044] = numeric::truncToInt(value);
        return;
    }
    if (id >= 10048 && id <= 10051) {
        m_globals[id - 10048] = value;
        return;
    }
    if (id >= 10077 && id <= 10080) {
        thread.scratch[id - 10077] = value;
        return;
    }
    switch (id) {
    case 10026: enemy.x = stored(); break;
    case 10027: enemy.y = stored(); break;
    case 10052: enemy.angle = stored(); break;
    case 10053: enemy.angularVelocity = stored(); break;
    case 10054: enemy.speed = stored(); break;
    case 10055: enemy.acceleration = stored(); break;
    case 10077: enemy.minX = stored(); break;
    case 10078: enemy.minY = stored(); break;
    default: recordFault(vm::Fault::UnsupportedVariable, 0, 0, -id); break;
    }
}

void LevelSimulation::emit(Enemy &enemy, Thread &thread, const format::EclCommand &command)
{
    const auto f = [&](int index) {
        return command.mask & (1u << (index + 1)) ? variable(enemy, thread, int(command.number(index)))
                                                  : command.number(index);
    };
    const auto n = [&](int index) {
        return command.mask & (1u << (index + 1)) ? int(variable(enemy, thread, command.integer(index)))
                                                  : command.integer(index);
    };
    const int rawType = command.integer(0) & 65535;
    const int rawColor = (unsigned(command.integer(0)) >> 16) & 65535;
    const int type = command.mask & 1 ? int(variable(enemy, thread, rawType)) : rawType;
    const int color = command.mask & 2 ? int(variable(enemy, thread, rawColor)) : rawColor;
    const int count = n(1);
    const int layers = n(2);
    const int mode = command.opcode - 86;
    if (type < 0 || type >= 24 || count < 0 || count > 1600 || layers < 0 || layers > 1600) {
        recordFault(vm::Fault::InvalidOperand, command.opcode, command.offset, type);
        return;
    }
    const float angle = f(5);
    const float spread = f(6);
    const float speed = f(3);
    const float speed2 = f(4);
    const float aim = numeric::atan2(
        numeric::sub(playerY, numeric::add(enemy.y, enemy.shotY)),
        numeric::sub(playerX, numeric::add(enemy.x, enemy.shotX)));
    for (int layer = 0; layer < layers; ++layer)
        for (int index = 0; index < count; ++index) {
            if (m_bullets.size() >= 1600) return;
            float direction = angle;
            float velocity = layers <= 1
                ? speed
                : numeric::sub(speed, numeric::mul(numeric::div(numeric::sub(speed, speed2),
                                                                float(layers)),
                                                   float(layer)));
            if (mode <= 1) {
                float offset = count & 1
                    ? numeric::mul(float((index + 1) / 2), spread)
                    : numeric::add(numeric::mul(float(index / 2), spread),
                                   numeric::mul(spread, .5f));
                if (index & 1) offset = numeric::neg(offset);
                direction = numeric::add(direction, numeric::add(offset, mode == 0 ? aim : 0.0f));
            } else if (mode <= 3) {
                direction = numeric::add(
                    direction,
                    numeric::add(numeric::div(numeric::mul(float(index), 2.0f * float(numeric::kPi)),
                                              float(count)),
                                 numeric::add(numeric::mul(float(layer), spread),
                                              mode == 2 ? aim : 0.0f)));
            } else if (mode <= 5) {
                direction = numeric::add(
                    direction,
                    numeric::add(numeric::div(float(numeric::kPi), float(count)),
                                 numeric::add(numeric::div(numeric::mul(float(index),
                                                                       2.0f * float(numeric::kPi)),
                                                           float(count)),
                                              mode == 4 ? aim : 0.0f)));
            } else if (mode == 6) {
                direction = numeric::add(numeric::mul(unit(), numeric::sub(angle, spread)), spread);
            } else if (mode == 7) {
                direction = numeric::add(
                    direction,
                    numeric::add(numeric::div(numeric::mul(float(index), 2.0f * float(numeric::kPi)),
                                              float(count)),
                                 numeric::mul(float(layer), spread)));
                velocity = numeric::add(numeric::mul(unit(), numeric::sub(speed, speed2)), speed2);
            } else {
                direction = numeric::add(numeric::mul(unit(), numeric::sub(angle, spread)), spread);
                velocity = numeric::add(numeric::mul(unit(), numeric::sub(speed, speed2)), speed2);
            }
            const int flags = n(7);
            Bullet bullet;
            bullet.id = m_nextId++;
            bullet.x = numeric::add(enemy.x, enemy.shotX);
            bullet.y = numeric::add(enemy.y, enemy.shotY);
            bullet.type = type;
            bullet.color = color;
            bullet.angle = numeric::wrapAngle(direction);
            bullet.speed = velocity;
            bullet.radius = numeric::div(bulletRadius(type), 2);
            bullet.flags = flags;
            bullet.extensions = enemy.extensions;
            bullet.sound = enemy.transformSound;
            // Appearance interrupt 7/8/9 starts four velocity steps behind.
            if (bullet.flags & 14) {
                bullet.x = numeric::sub(bullet.x, numeric::mul(numeric::mul(numeric::cos(direction),
                                                                            velocity), 4.0f));
                bullet.y = numeric::sub(bullet.y, numeric::mul(numeric::mul(numeric::sin(direction),
                                                                            velocity), 4.0f));
            }
            bullet.vx = numeric::mul(numeric::cos(direction), velocity);
            bullet.vy = numeric::mul(numeric::sin(direction), velocity);
            bullet.appearance = bullet.flags & 8 ? 25 : bullet.flags & 4 ? 15 : bullet.flags & 2 ? 8 : 0;
            m_bullets.push_back(bullet);
            ++emitted;
        }
    if (n(7) & 512) {
        sound(enemy.shotSound, numeric::add(enemy.x, enemy.shotX));
        ++volleyEvent;
        volleyX = numeric::add(enemy.x, enemy.shotX);
        volleyY = numeric::add(enemy.y, enemy.shotY);
    }
}

void LevelSimulation::run(Enemy &enemy, Thread &thread)
{
    if (thread.script < 0) return;
    // The retail VM advances the script clock every tick, including ticks the
    // thread spends inside a wait; the wait timer is a separate countdown.
    const auto body = [&]() {
    for (int budget = 0; budget < 4096; ++budget) {
        if (thread.wait > 0) {
            thread.wait = numeric::max(0.0f, numeric::sub(thread.wait, timeScale));
            return;
        }
        if (thread.script < 0 || thread.script >= int(m_ecl.scripts.size())) {
            recordFault(vm::Fault::MissingResource, 0, 0, thread.script);
            thread.script = -1;
            return;
        }
        const auto &script = m_ecl.scripts[thread.script];
        if (thread.pc >= script.commands.size()) {
            thread.script = -1;
            return;
        }
        const auto &command = script.commands[thread.pc];
        if (command.terminator()) {
            thread.script = -1;
            return;
        }
        if (float(command.time) > numeric::floorValue(thread.time)) break;
        ++executed;
        ++thread.pc;
        const auto f = [&](int index) {
            return command.mask & (1u << index) ? variable(enemy, thread, int(command.number(index)))
                                                : command.number(index);
        };
        const auto n = [&](int index) {
            return command.mask & (1u << index) ? int(variable(enemy, thread, command.integer(index)))
                                                : command.integer(index);
        };
        const auto put = [&](float value, bool integer) {
            assign(enemy, thread, integer ? command.integer(0) : int(command.number(0)), value, integer);
        };
        const auto jump = [&](int time, int relative) {
            const int destination = int(command.offset) + relative;
            const auto it = std::find_if(script.commands.begin(), script.commands.end(),
                                         [&](const auto &candidate) {
                                             return int(candidate.offset) == destination;
                                         });
            if (it == script.commands.end()) {
                recordFault(vm::Fault::UnresolvedJump, command.opcode, command.offset, destination);
                return;
            }
            thread.pc = std::size_t(it - script.commands.begin());
            thread.time = float(time);
        };
        switch (command.opcode) {
        case 0: break;
        case 1: enemy.alive = false; thread.script = -1; return;
        case 2: thread.wait = float(n(0)); break;
        case 4: jump(command.integer(0), command.integer(1)); break;
        case 5: {
            const int id = command.integer(2);
            const int value = numeric::wrapSub(numeric::truncToInt(variable(enemy, thread, id)), 1);
            assign(enemy, thread, id, float(value), true);
            if (value > 0) jump(command.integer(0), command.integer(1));
            break;
        }
        case 6: put(float(n(1)), true); break;
        case 7: put(f(1), false); break;
        case 10:
            put(numeric::add(variable(enemy, thread, command.integer(0)), float(n(1))), true);
            break;
        case 15:
            put(numeric::add(variable(enemy, thread, int(command.number(0))), f(1)), false);
            break;
        case 16:
            put(numeric::sub(variable(enemy, thread, int(command.number(0))), f(1)), false);
            break;
        case 17:
            put(numeric::mul(variable(enemy, thread, int(command.number(0))), f(1)), false);
            break;
        // 0x14..0x18 are the two-operand integer forms; 0x15 is SUBTRACT and
        // 0x16 is MULTIPLY in the retail table, which LV1-4 script 4 uses to
        // derive its volley wait from the photo count (12 - 3*photos).
        case 20: put(float(numeric::wrapAdd(n(1), n(2))), true); break;
        case 21: put(float(numeric::wrapSub(n(1), n(2))), true); break;
        case 22: put(float(numeric::wrapMul(n(1), n(2))), true); break;
        case 23: {
            const int value = n(1), divisor = n(2);
            put(divisor ? float(value / divisor) : 0, true);
            break;
        }
        case 24: {
            const int value = n(1), divisor = n(2);
            put(divisor ? float(value % divisor) : 0, true);
            break;
        }
        case 25: put(numeric::add(f(1), f(2)), false); break;
        case 26: put(numeric::sub(f(1), f(2)), false); break;
        case 27: put(numeric::mul(f(1), f(2)), false); break;
        case 28: {
            const float numerator = f(1), denominator = f(2);
            put(denominator ? numeric::div(numerator, denominator) : 0, false);
            break;
        }
        case 30: {
            const int id = command.integer(0);
            assign(enemy, thread, id,
                   numeric::add(variable(enemy, thread, id), 1.0f), true);
            break;
        }
        case 38: {
            // ECL 0x26: polar velocity. arg0/arg1 are the two destination
            // variables, arg2 the angle and arg3 the speed.
            const float angle = f(2), speed = f(3);
            assign(enemy, thread, int(command.number(0)),
                   numeric::mul(numeric::cos(angle), speed), false);
            assign(enemy, thread, int(command.number(1)),
                   numeric::mul(numeric::sin(angle), speed), false);
            break;
        }
        case 44: if (n(0) < n(1)) jump(command.integer(2), command.integer(3)); break;
        case 47: if (f(0) <= f(1)) jump(command.integer(2), command.integer(3)); break;
        case 51: if (f(0) >= f(1)) jump(command.integer(2), command.integer(3)); break;
        case 52:
            thread.stack.push_back({thread.script, thread.pc, thread.time, thread.integers,
                                    thread.floats, thread.counters, thread.intArguments,
                                    thread.arguments, thread.scratch});
            thread.script = n(0);
            thread.pc = 0;
            thread.time = 0;
            thread.intArguments = m_globalIntegers;
            thread.arguments = m_globals;
            break;
        case 53:
            if (thread.stack.empty()) {
                thread.script = -1;
                return;
            } else {
                const auto frame = thread.stack.back();
                thread.stack.pop_back();
                thread.script = frame.script;
                thread.pc = frame.pc;
                thread.time = frame.time;
                thread.integers = frame.integers;
                thread.floats = frame.floats;
                thread.counters = frame.counters;
                thread.intArguments = frame.intArguments;
                thread.arguments = frame.arguments;
                thread.scratch = frame.scratch;
            }
            break;
        case 54: enemy.animation = n(0); break;
        case 55: enemy.animation = n(0); enemy.lastDirection = -1; break;
        case 62: enemy.animation = 7; break;
        case 63: enemy.x = f(0); enemy.y = f(1); enemy.vx = enemy.vy = 0; break;
        case 64:
            enemy.startX = enemy.x;
            enemy.startY = enemy.y;
            enemy.deltaX = numeric::sub(f(2), enemy.x);
            enemy.deltaY = numeric::sub(f(3), enemy.y);
            enemy.moveTime = 0;
            enemy.moveDuration = float(n(0));
            enemy.ease = n(1);
            enemy.moveMode = 2;
            break;
        case 65:
            enemy.angle = f(0);
            enemy.speed = f(1);
            enemy.moveMode = 1;
            enemy.moveDuration = 0;
            break;
        case 66: {
            const float angle = f(2), speed = f(3);
            enemy.startX = enemy.x;
            enemy.startY = enemy.y;
            enemy.moveTime = 0;
            enemy.moveDuration = float(n(0));
            enemy.ease = n(1);
            enemy.deltaX = numeric::mul(numeric::mul(numeric::cos(angle), speed),
                                        enemy.moveDuration);
            enemy.deltaY = numeric::mul(numeric::mul(numeric::sin(angle), speed),
                                        enemy.moveDuration);
            enemy.moveMode = 2;
            break;
        }
        case 67: {
            float angle = playerX >= enemy.x
                ? numeric::sub(numeric::div(numeric::mul(unit(), float(numeric::kPi)), 2),
                               numeric::div(float(numeric::kPi), 4))
                : numeric::wrapAngle(numeric::add(numeric::div(numeric::mul(unit(),
                                                                           float(numeric::kPi)),
                                                              2),
                                                  numeric::mul(3.0f,
                                                               numeric::div(float(numeric::kPi),
                                                                            4))));
            if (numeric::add(enemy.minX, 96) > enemy.x &&
                (angle > numeric::div(float(numeric::kPi), 2) ||
                 angle < numeric::div(-float(numeric::kPi), 2)))
                angle = numeric::wrapAngle(numeric::sub(float(numeric::kPi), angle));
            if (numeric::sub(enemy.maxX, 96) < enemy.x &&
                angle > numeric::div(-float(numeric::kPi), 2) &&
                angle < numeric::div(float(numeric::kPi), 2))
                angle = numeric::wrapAngle(numeric::sub(float(numeric::kPi), enemy.angle));
            if (numeric::add(enemy.minY, 48) > enemy.y && angle < 0) angle = numeric::neg(angle);
            if (numeric::sub(enemy.maxY, 48) < enemy.y && angle > 0) angle = numeric::neg(angle);
            enemy.startX = enemy.x;
            enemy.startY = enemy.y;
            enemy.moveTime = 0;
            enemy.moveDuration = float(n(0));
            enemy.ease = n(1);
            enemy.deltaX = numeric::mul(numeric::mul(numeric::cos(angle), f(2)),
                                        enemy.moveDuration);
            enemy.deltaY = numeric::mul(numeric::mul(numeric::sin(angle), f(2)),
                                        enemy.moveDuration);
            enemy.moveMode = 2;
            break;
        }
        case 70: enemy.angularVelocity = f(0); enemy.moveMode = 1; break;
        case 71: enemy.acceleration = f(0); enemy.moveMode = 1; break;
        case 75:
            enemy.boundsEnabled = true;
            enemy.minX = f(0);
            enemy.minY = f(1);
            enemy.maxX = f(2);
            enemy.maxY = f(3);
            break;
        case 76: enemy.boundsEnabled = false; break;
        case 77: enemy.hitWidth = f(0); enemy.hitHeight = f(1); break;
        case 80:
        case 81: {
            // Retail 0x50/0x51 poke the enemy flag window directly:
            //   bit0 -> 0x40 (set on 0x51, clear on 0x50)
            //   bit1 -> 0x4  (contact collision)
            //   bit2 -> 0x8  (second default-on flag, meaning unrecovered)
            //   bit3 -> 0x10 (0x51 clears, 0x50 sets: draw/target suppression)
            //   bit4 -> 0x4000000 (0x51 clears, 0x50 sets: slot-free bounds test)
            //   bit5 -> +11256 bit 8 (0x51 clears, 0x50 sets)
            // Previous code treated bit3 as the photo-exclusion flag and bit4 as
            // the draw-disable flag, which made LV1-3's satellite enemies (ECL
            // 0x50 with 11) visible and kept them out of the shutter scan.
            const int flags = n(0);
            const bool set = command.opcode == 81;
            if (flags & 1) enemy.attackable = set;
            if (flags & 2) enemy.collision = set;
            if (flags & 8) enemy.hidden = !set;
            if (flags & 16) enemy.boundsCheck = !set;
            break;
        }
        case 83: spawn(n(0), enemy.x, enemy.y, &thread); break;
        case 84:
            if (enemy.spawnGate > 0) {
                const float x = numeric::add(enemy.x, f(1));
                const float y = numeric::add(enemy.y, f(2));
                spawn(n(0), x, y, &thread);
            }
            break;
        case 85:
            for (auto &other : m_enemies)
                if (!other.boss) other.alive = false;
            break;
        case 86: case 87: case 88: case 89: case 90: case 91:
        case 92: case 93: case 94:
            emit(enemy, thread, command);
            break;
        case 100: enemy.shotX = f(0); enemy.shotY = f(1); break;
        case 101: {
            const int slot = n(0);
            if (slot < 0 || slot >= 18) {
                recordFault(vm::Fault::InvalidOperand, command.opcode, command.offset, slot);
                break;
            }
            enemy.extensions[slot] = {n(1), n(3), n(4), f(5), f(6), n(2)};
            break;
        }
        case 102: m_bullets.clear(); break;
        case 103: enemy.shotSound = n(0); enemy.transformSound = n(1); break;
        case 104: {
            sound(14);
            m_effects.push_back({1000, enemy.x, enemy.y, enemy.id});
            m_effects.push_back({1001, enemy.x, enemy.y, enemy.id});
            // Retail stores the declaration streak handle on the declaring
            // enemy (enemy+19644) and the per-frame enemy update drags the
            // animation toward that enemy, so the ring must keep its owner id.
            m_effects.push_back({210, enemy.x, enemy.y, enemy.id});
            std::string name;
            for (std::size_t i = 0; i < command.args.size() && i < 48; ++i) {
                const unsigned byte = static_cast<unsigned char>(command.args[i]) ^ 0xAAu;
                if (!byte) break;
                name.push_back(char(byte));
            }
            if (!name.empty()) {
                spellName = std::move(name);
                spellActive = true;
                ++spellEvent;
            }
            break;
        }
        case 105:
            m_effects.push_back({1002, enemy.x, enemy.y, enemy.id});
            if (spellActive) {
                spellActive = false;
                ++spellEvent;
            }
            break;
        case 106: sound(n(0), enemy.x); break;
        case 108: {
            const int slot = n(1);
            if (slot >= 0 && slot < 32) {
                enemy.interrupts[slot] = n(0);
            } else {
                recordFault(vm::Fault::InvalidOperand, command.opcode, command.offset, slot);
            }
            break;
        }
        case 109: enemy.boss = n(0) >= 0; break;
        case 112: enemy.word10330 = n(0); break;
        case 114: remaining = float(n(0)); break;
        case 115: {
            const int slot = n(0);
            if (slot < 0 || slot >= 10) {
                recordFault(vm::Fault::InvalidOperand, command.opcode, command.offset, slot);
                break;
            }
            enemy.photoThreshold[slot] = n(1);
            enemy.photoScripts[slot] = n(2);
            break;
        }
        case 117: {
            const int slot = n(0), sub = n(1);
            if (slot < 0 || slot >= 16) {
                recordFault(vm::Fault::InvalidOperand, command.opcode, command.offset, slot);
                break;
            }
            Thread async;
            if (sub >= 0) {
                async = thread;
                async.script = sub;
                async.pc = 0;
                async.time = 0;
                async.stack.clear();
            }
            enemy.async[slot] = async;
            break;
        }
        case 118:
            switch (n(0)) {
            case 0:
                m_effects.push_back({291, enemy.x, enemy.y});
                for (int i = 0; i < 32; ++i) {
                    m_effects.push_back({290, enemy.x, enemy.y});
                    // bullet.anm script 290 opens with ANM opcode 40 (variable
                    // 10000 = random % 15). 0x404B80 executes script t=0 right
                    // away, so each creation consumes one word_4BE208 draw.
                    (void)random();
                }
                sound(18);
                timeScale = .25f;
                break;
            case 1:
                // Retail callback 1 spawns script 0 at every active bullet
                // whose current ANM sprite is at least 64 px wide. The widest
                // sprite in bullet.anm is 30 px, so it never fires in TH095.
                break;
            default:
                recordFault(vm::Fault::MissingService, command.opcode, command.offset, n(0));
                break;
            }
            break;
        case 124: enemy.word566 = n(0); break;
        case 141: requiredPhotos = n(0); break;
        case 142:
            if (!cleared) {
                dead = true;
                failureAge = 30;
            }
            break;
        case 143:
            enemy.spawnCircle = (n(0) & 1) != 0;
            enemy.spawnCircleTimer = float(n(1));
            break;
        case 144:
            enemy.warning = enemy.warningDuration = float(n(0));
            m_effects.push_back({293, enemy.x, enemy.y, enemy.id});
            // The warning ring (bullet.anm 293) draws one random value when the
            // object opens, so the global stream must advance here exactly as it
            // does for the 290 explosion script.
            (void)random();
            sound(45);
            break;
        case 149: scoreMultiplier = f(0); break;
        case 150:
            m_effects.push_back({n(0), numeric::add(enemy.x, enemy.shotX),
                                 numeric::add(enemy.y, enemy.shotY)});
            break;
        default:
            recordFault(vm::Fault::UnsupportedInstruction, command.opcode, command.offset);
            break;
        }
        if (!enemy.alive) return;
        if (budget == 4095) {
            recordFault(vm::Fault::BudgetExhausted, 0, 0);
            return;
        }
    }
    };
    body();
    thread.time = numeric::add(thread.time, timeScale);
}

} // namespace th95::scene
