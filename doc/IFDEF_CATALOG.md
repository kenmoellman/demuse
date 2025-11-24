# Complete Catalog of #ifdef Usage in deMUSE

Generated: 2025-01-23
Purpose: Document all preprocessor directives for migration to runtime configuration

---

## Summary Statistics

- **Total unique symbols**: 98
- **Most used**: USE_UNIV (55 occurrences)
- **Header guards**: ~20 files
- **Feature flags**: ~30 symbols
- **Platform-specific**: ~15 symbols

---

## MIGRATION DECISIONS (2025-01-23)

### ✅ Always Enable - Remove #ifdef and #define

These features will be permanently enabled. Code will be simplified by removing conditional compilation.

| Symbol | Count | Decision | Notes |
|--------|-------|----------|-------|
| **USE_BLACKLIST** | 10 | **REMOVE #ifdef** | Always enable IP blacklist system |
| **PUEBLO_CLIENT** | 12 | **REMOVE #ifdef** | Always enable Pueblo HTML client support |
| **BOOT_GUEST** | 1 | **REMOVE #ifdef** | Always enable guest timeouts. Timeout duration remains configurable |
| **WHO_IDLE_COLOR** | 1 | **REMOVE #ifdef** | Always enable color for idle players on WHO |
| **RANDOM_WELCOME** | 1 | **REMOVE #ifdef** | Always enable random welcome messages |
| **CR_UNIDLE** | 1 | **REMOVE #ifdef** | Always enable carriage return unidle behavior |
| **DBTOP_POW** | 2 | **REMOVE #ifdef** | Always enable power requirement for @dbtop |
| **USE_UNIV** | 55 | **REMOVE #ifdef** | Always enable universe system |

**Action Items:**
- Search and remove all `#ifdef USE_BLACKLIST` / `#endif` pairs
- Search and remove all `#ifdef PUEBLO_CLIENT` / `#endif` pairs
- Search and remove all `#ifdef BOOT_GUEST` / `#endif` pairs
- Search and remove all `#ifdef WHO_IDLE_COLOR` / `#endif` pairs
- Search and remove all `#ifdef RANDOM_WELCOME` / `#endif` pairs
- Search and remove all `#ifdef CR_UNIDLE` / `#endif` pairs
- Search and remove all `#ifdef DBTOP_POW` / `#else` / `#endif` (may have alternates)
- Search and remove all `#ifdef USE_UNIV` / `#endif` pairs (55 occurrences!)
- Remove corresponding `#define` or `#undef` from config/config.h
- Test compilation after each removal
- Verify features work correctly

### 🔄 Convert to Different Mechanism

| Symbol | Count | New Mechanism | Notes |
|--------|-------|---------------|-------|
| **ALLOW_COM_NP** | 2 | **Object Power** | Convert to wizard-settable object power instead of compile-time flag |

**Action Items:**
- Define new power POW_COM_TALK (or similar)
- Replace `#ifdef ALLOW_COM_NP` checks with `has_power(thing, POW_COM_TALK)`
- Add power to powers table in database
- Add power to config.h power list
- Document in help files

### ⏸️ Leave as #ifdef (For Now)

These will remain as compile-time flags pending further review.

| Symbol | Count | Status | Reason |
|--------|-------|--------|--------|
| **USE_CID_PLAY** | 11 | **KEEP #ifdef** | Needs further evaluation |
| **USE_COMBAT** | 18 | **KEEP #ifdef** | May remove later, system incomplete |
| **USE_COMBAT_TM97** | 8 | **KEEP #ifdef** | Depends on USE_COMBAT decision |
| **USE_INCOMING** | 5 | **KEEP #ifdef** | Needs security review |
| **USE_OUTGOING** | 4 | **KEEP #ifdef** | Needs examination and security review |

### 🗑️ Mark for Later Removal

These features will be removed from the codebase in the future. Do NOT add to config system.

| Symbol | Count | Status | Notes |
|--------|-------|--------|-------|
| **SHRINK_DB** | 6 | **DO NOT CONFIG** | Command will be removed later, keep as #ifdef |
| **USE_RLPAGE** | 3 | **DO NOT CONFIG** | RemoteLink paging obsolete, will be removed later |

### ❌ Remove Entirely

| Symbol | Count | Action | Notes |
|--------|-------|--------|-------|
| **USE_SPACE** | 3 | **DELETE CODE** | Remove all space system code entirely |

**Action Items:**
- Search for all `#ifdef USE_SPACE` blocks
- Delete the entire blocks (including code inside)
- Remove any space-related functions/structures
- Remove `#define USE_SPACE` from config.h

---

## Summary of Actions

### Immediate Tasks (High Priority)

1. **Remove 8 feature flags** (simplify ~90 occurrences)
   - USE_BLACKLIST (10)
   - PUEBLO_CLIENT (12)
   - BOOT_GUEST (1)
   - WHO_IDLE_COLOR (1)
   - RANDOM_WELCOME (1)
   - CR_UNIDLE (1)
   - DBTOP_POW (2)
   - USE_UNIV (55) ← **BIGGEST**

2. **Convert 1 flag to power system**
   - ALLOW_COM_NP → POW_COM_TALK

3. **Delete 1 obsolete system**
   - USE_SPACE (3)

### Later Tasks (Lower Priority)

4. **Keep under review** (26 occurrences)
   - USE_CID_PLAY (11)
   - USE_COMBAT + variants (26)
   - USE_INCOMING (5)
   - USE_OUTGOING (4)

5. **Mark for deprecation** (9 occurrences)
   - SHRINK_DB (6)
   - USE_RLPAGE (3)

---

## Category 1: FEATURE FLAGS (Can become runtime config)

### High Priority - Simple Runtime Conversion

| Symbol | Count | Description | Migration Difficulty |
|--------|-------|-------------|---------------------|
| **USE_BLACKLIST** | 10 | IP blacklist system | EASY - simple if statements |
| **WHO_IDLE_COLOR** | 1 | Color idle players on WHO | EASY - display logic only |
| **ALLOW_COM_NP** | 2 | Allow non-players on channels | EASY - permission check |
| **SHRINK_DB** | 6 | Enable @shrink command | EASY - command availability |
| **USE_RLPAGE** | 3 | Email paging system | EASY - feature toggle |
| **PUEBLO_CLIENT** | 12 | Pueblo HTML client support | EASY - output formatting |
| **RANDOM_WELCOME** | 1 | Random welcome messages | EASY - startup logic |
| **CR_UNIDLE** | 1 | Carriage return unidles | EASY - input handling |
| **BOOT_GUEST** | 1 | Boot guests at idle/reboot | EASY - timer logic |
| **BOOT_GUESTS** | 1 | (duplicate of above) | EASY |
| **DBTOP_POW** | 2 | Require power for @dbtop | EASY - permission check |

**Migration plan**: Replace `#ifdef FEATURE` with `if (feature_name)`

### Medium Priority - Structural Features

| Symbol | Count | Description | Migration Difficulty |
|--------|-------|-------------|---------------------|
| **USE_UNIV** | 55 | Universe system | MEDIUM - struct fields always compiled |
| **USE_COMBAT** | 18 | Combat system (incomplete) | MEDIUM - struct fields always compiled |
| **USE_COMBAT_TM97** | 8 | TinyMUSE '97 combat variant | MEDIUM - related to USE_COMBAT |
| **USE_CID_PLAY** | 11 | ConcID play system | MEDIUM - some struct changes |
| **USE_INCOMING** | 5 | Connect to non-player objects | MEDIUM - attribute definitions |
| **USE_OUTGOING** | 4 | Outgoing network connections | MEDIUM - network code |
| **USE_SPACE** | 3 | Space system | MEDIUM - structural |

**Migration plan**:
1. Always compile struct fields (universe, combat, etc.)
2. Add runtime checks before using features
3. Initialize pointers to NULL when disabled
4. Memory overhead acceptable for flexibility

### Low Priority - Optional Features

| Symbol | Count | Description | Migration Difficulty |
|--------|-------|-------------|---------------------|
| **BLINK** | 5 | Blinking text support | LOW - cosmetic |
| **PURGE_OLDMAIL** | 1 | Auto-purge old mail | LOW - cron feature |
| **HOME_ACROSS_ZONES** | 1 | Allow home across zones | LOW - movement check |
| **ALLOW_EXEC** | 5 | Allow shell execution | SECURITY - keep compile-time |
| **EMERGENCY_BYPASS_PASSWORD** | 2 | Emergency admin access | SECURITY - keep compile-time |

---

## Category 2: COMPILE-TIME ONLY (Must stay as #ifdef)

### Memory/Binary Structure Changes

| Symbol | Count | Description | Why Compile-Time |
|--------|-------|-------------|------------------|
| **MEMORY_DEBUG_LOG** | 36 | Malloc debugging wrapper | Wraps all malloc/free calls |
| **DBCOMP** | 4 | Compressed database files | Changes file I/O paths |
| **USE_VFORK** | 3 | Use vfork instead of fork | System call replacement |
| **MALLOCDEBUG** | 3 | Alternative malloc debugging | Links different library |
| **TEST_MALLOC** | 2 | Malloc testing | Debug only |

**Recommendation**: Keep as #ifdef, document in database as `internal_` with note

### Platform-Specific Code

| Symbol | Count | Description | Why Compile-Time |
|--------|-------|-------------|------------------|
| **XENIX** | 3 | XENIX Unix compatibility | Platform detection |
| **SYSV** | 1 | System V Unix | Platform detection |
| **linux** | 1 | Linux-specific code | Platform detection |
| **ultrix** | 2 | Ultrix Unix | Platform detection |
| **NeXT** | 2 | NeXT/OpenStep | Platform detection |
| **__GLIBC__** | 2 | GNU C Library | Platform detection |
| **__sgi** | 1 | SGI IRIX | Platform detection |
| **_AIX** | 1 | IBM AIX | Platform detection |
| **void_signal_type** | 8 | Signal handler return type | Compiler difference |
| **__STDC__** | 2 | ANSI C compliance | Compiler feature |
| **__GNUC__** | 1+ | GNU C compiler | Compiler detection |
| **__cplusplus** | 2 | C++ compatibility | Language detection |

**Recommendation**: Keep as #ifdef, these are platform/compiler detection

### Network/System Configuration

| Symbol | Count | Description | Why Compile-Time |
|--------|-------|-------------|------------------|
| **MULTIHOME** | 2 | Multi-homed server | Network config at compile |
| **HOST_LOOKUPS** | 2 | Reverse DNS lookups | Could be runtime |
| **RESOCK** | 7 | Socket reinitialization | Platform-specific |

**Recommendation**:
- MULTIHOME: Keep compile-time (affects initialization)
- HOST_LOOKUPS: Could be runtime
- RESOCK: Keep compile-time (platform-specific)

---

## Category 3: HEADER GUARDS (Required)

Standard C header include guards - must remain:

| Symbol | Files | Purpose |
|--------|-------|---------|
| **_LOADED_CONFIG_** | 1 | config.h |
| **__DB_H** | 1 | db.h |
| **_EXTERNS_H_** | 1 | externs.h |
| **_ATTR_H_** | 1 | attr.h |
| **_OBJECT_H_** | 1 | object.h |
| **PARSER_H** | 1 | parser.h |
| **__IDENT_H__** | 1 | ident.h |
| **HASH_TABLE_H** | 1 | hash_table.h |
| **_ZONES_H_** | 1 | zones.h |
| **_NET_H** | 1 | net.h |
| **__SPEECH_H__** | 1 | speech.h |
| **_IDLE_H_** | 1 | idle.h |
| **WE_DID_FIFO_H** | 1 | fifo.h |
| **_INFO_H_** | 1 | info.h |
| **__LOG_H** | 1 | log.h |
| **_ECONOMY_H_** | 1 | economy.h |
| **IO_INTERNAL_H** | 1 | io_internal.h |
| **__DATETIME_H__** | 1 | datetime.h |
| **__EMAIL_CONFIG_H__** | 1 | config.h (email section) |

**Recommendation**: Required by C, must stay

---

## Category 4: CONFIGURATION DEFAULTS (Special cases)

| Symbol | Count | Description | Action |
|--------|-------|-------------|--------|
| **DEFDBREF** | 4 | dbref typedef guard | Keep - type definition |
| **DECLARE_ATTR** | 1 | Attribute declaration mode | Keep - macro system |
| **ARG_DELIMITER** | 1 | Check if defined | Keep - token definition |
| **POSE_TOKEN** | 1 | Check if defined | Keep - token definition |
| **ValidObject** | 1 | Check if macro defined | Keep - macro function |
| **GoodObject** | 1 | Check if macro defined | Keep - macro function |

---

## Category 5: OPTIONAL DEFAULTS (Can remove)

These check if something is defined, provide default if not:

| Symbol | Purpose |
|--------|---------|
| **SMTP_SERVER** | Email config default |
| **SMTP_PORT** | Email config default |
| **SMTP_USE_SSL** | Email config default |
| **SMTP_USERNAME** | Email config default |
| **SMTP_PASSWORD** | Email config default |
| **SMTP_FROM** | Email config default |
| **MAX_EMAILS_PER_DAY** | Email limit default |
| **EMAIL_COOLDOWN** | Email limit default |
| **MAX_EMAIL_LENGTH** | Email limit default |
| **MAX_PAGE_LEN** | Page limit default |
| **MAX_PAGE_TARGETS** | Page limit default |
| **IDBUFSIZE** | Ident buffer size |
| **IDPORT** | Ident port |
| **FI_BSIZE** | FIFO buffer size |
| **WCREAT** | File creation mode |

**Recommendation**: Move to database config with defaults

---

## Category 6: SYSTEM/LIBRARY DETECTION

Checking for system features:

| Symbol | Purpose |
|--------|---------|
| **S_IFMT** | File mode mask |
| **FD_SETSIZE** | Select() max FDs |
| **SOCK_STREAM** | Socket type |
| **IPPROTO_IP** | IP protocol |
| **__sys_types_h** | sys/types.h included |
| **fileno** | fileno() macro |
| **NO_PROTO_VARARGS** | Varargs support |
| **NO_HUGE_RESOLVER_CODE** | Resolver code size |
| **IN_LIBIDENT_SRC** | Building libident |
| **IS_STDC** | ANSI C mode |
| **__P** | Prototype macro |
| **HAVE_STRPTIME** | strptime() available |

**Recommendation**: Keep as #ifdef - system compatibility

---

## Category 7: DEBUG/DEVELOPMENT

| Symbol | Count | Purpose |
|--------|-------|---------|
| **DEBUG** | 2 | Debug code |
| **MODIFIED** | 1 | Modified version flag |
| **BETA** | 1 | Beta version flag |
| **POW_DBTOP** | 1 | Power constant check |
| **__DO_DB_C__** | 1 | Conditional in header |

**Recommendation**: DEBUG could be runtime, others keep

---

## Migration Priority

### Phase 1: Simple Feature Flags (Immediate)
```
USE_BLACKLIST
WHO_IDLE_COLOR
ALLOW_COM_NP
SHRINK_DB
USE_RLPAGE
PUEBLO_CLIENT
RANDOM_WELCOME
CR_UNIDLE
BOOT_GUEST
DBTOP_POW
```
**Effort**: Low - straightforward search/replace
**Impact**: 10+ feature flags become runtime
**Risk**: Low - no structural changes

### Phase 2: Structural Features (Short-term)
```
USE_UNIV (55 uses - biggest impact)
USE_COMBAT
USE_CID_PLAY
USE_INCOMING
USE_OUTGOING
USE_SPACE
```
**Effort**: Medium - requires struct changes
**Impact**: Major features become runtime toggleable
**Risk**: Medium - need thorough testing

### Phase 3: Configuration Defaults (Medium-term)
```
Email config defaults
Page limits
Buffer sizes
```
**Effort**: Low - move to database
**Impact**: All limits configurable
**Risk**: Low

### Phase 4: Optional (Long-term)
```
HOST_LOOKUPS (could be runtime)
DEBUG (could be runtime)
BLINK
HOME_ACROSS_ZONES
```
**Effort**: Low
**Impact**: Minor conveniences
**Risk**: Low

---

## Files with Most #ifdef Usage

1. **config/config.h** - 30+ (mostly power/class definitions)
2. **src/io/nalloc.c** - 36 (all MEMORY_DEBUG_LOG)
3. **src/db/object.c** - 15 (mostly USE_UNIV)
4. **src/muse/parser.c** - 24 (USE_COMBAT, USE_UNIV, commands)
5. **src/db/db_io.c** - 6 (USE_UNIV, database I/O)
6. **src/hdrs/ident.h** - 15 (platform detection)
7. **src/io/sock.c** - 7 (networking features)

---

## Recommendations

### Immediate Actions

1. **Create feature flag infrastructure** (config.c, externs.h)
2. **Migrate Phase 1 flags** to runtime (10 flags, ~50 occurrences)
3. **Document compile-time-only flags** in database with `internal_` prefix
4. **Test each migration** thoroughly before moving to next

### Medium-term Actions

1. **Refactor USE_UNIV** (55 occurrences)
   - Always compile universe fields into struct object
   - Add runtime checks before universe operations
   - Biggest single impact

2. **Refactor USE_COMBAT** (18 occurrences)
   - Always compile combat fields
   - Runtime enable/disable
   - Note: Combat system marked incomplete

3. **Move config defaults** to database
   - Email settings
   - Buffer sizes
   - Limits

### Long-term Actions

1. **Consider HOST_LOOKUPS** as runtime
   - Currently compile-time
   - Could be toggled for performance

2. **Evaluate MULTIHOME**
   - Currently compile-time
   - Might be possible as runtime with careful initialization

### Never Change

1. **Header guards** - Required by C
2. **Platform detection** - Compiler/OS differences
3. **MEMORY_DEBUG_LOG** - Wraps all allocations
4. **DBCOMP** - Changes file paths
5. **Type definitions** - typedef, struct layouts (when different)

---

## Migration Template

For each feature flag:

1. **Add to config.c**:
   ```c
   int feature_name = 1;  /* default */
   ```

2. **Add to externs.h**:
   ```c
   extern int feature_name;
   ```

3. **Add to apply_config_value()**:
   ```c
   else if (strcmp(name, "feature_name") == 0) {
       feature_name = val;
   }
   ```

4. **Add to database**:
   ```sql
   INSERT INTO config VALUES
   ('feature_name', 'true', 'bool', 'features',
    'Description of feature', FALSE, FALSE, TRUE, 'true', NOW(), NULL);
   ```

5. **Replace in code**:
   ```c
   // Before:
   #ifdef FEATURE_NAME
       do_something();
   #endif

   // After:
   if (feature_name) {
       do_something();
   }
   ```

6. **Test**:
   - Compile with feature on
   - Test with feature on
   - Test with feature off via @config
   - Verify no crashes/errors

---

## Estimated Migration Effort

- **Phase 1** (Simple flags): 2-3 days
- **Phase 2** (USE_UNIV): 3-5 days
- **Phase 2** (USE_COMBAT): 2-3 days
- **Phase 2** (Other structural): 2-3 days
- **Phase 3** (Config defaults): 1-2 days
- **Testing**: 3-5 days
- **Documentation**: 1-2 days

**Total**: 2-3 weeks for complete migration

---

## Next Steps

1. ✅ Complete this catalog
2. ⬜ Review and approve migration plan
3. ⬜ Set up feature flag infrastructure
4. ⬜ Start Phase 1 (simple flags)
5. ⬜ Test Phase 1 thoroughly
6. ⬜ Proceed to Phase 2
7. ⬜ Update documentation
8. ⬜ Update CLAUDE.md with new architecture
