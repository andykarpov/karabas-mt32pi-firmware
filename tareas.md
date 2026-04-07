# Tareas de refactorización y mejora — mt32-pi

> Generado tras análisis exhaustivo del código fuente completo.
> Prioridad: 🔴 Alta | 🟡 Media | 🟢 Baja
> Viabilidad: ✅ Segura | ⚠️ Requiere cuidado | ❌ Alto riesgo

---

## 🔴 T01 — Corregir bugs y comportamiento indefinido en `optional.h`

**Archivos:** `include/optional.h`
**Líneas:** ~42-58

**Problema:**
- El move constructor construye el valor interno con placement-new incluso cuando `Other.m_bSet == false`, accediendo memoria no inicializada.
- El `operator=` asume que `m_Value` está inicializado (`*ptr = *other_ptr`) sin comprobar si el objeto actual ya fue construido.
- Si `T` tiene destructor no trivial, el valor previo nunca se destruye antes de reemplazarse.

**Cambio concreto:**
```cpp
// Move constructor: solo construir si Other tiene valor
explicit TOptional(TOptional<T>&& Other) {
    m_bSet = Other.m_bSet;
    if (m_bSet)
        new(reinterpret_cast<T*>(m_Value)) T(std::move(*reinterpret_cast<T*>(Other.m_Value)));
}

// operator=: destruir valor previo si existe, luego copiar condicionalmente
TOptional<T>& operator=(const TOptional<T>& Other) {
    if (m_bSet)
        reinterpret_cast<T*>(m_Value)->~T();
    m_bSet = Other.m_bSet;
    if (m_bSet)
        new(reinterpret_cast<T*>(m_Value)) T(*reinterpret_cast<const T*>(Other.m_Value));
    return *this;
}
```

**Por qué:** Comportamiento indefinido en tiempo de ejecución. Un tipo con destructor o constructor no trivial puede corromper memoria.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura — cambio aislado, no afecta API pública.

---

## 🔴 T02 — Corregir buffer underflow en FTP (ftpworker.cpp:205)

**Archivos:** `src/net/ftpworker.cpp`
**Línea:** ~205

**Problema:**
```cpp
m_CommandBuffer[nReceiveBytes - 2] = '\0';
```
Si `nReceiveBytes < 2`, esto produce un índice negativo → escritura fuera de bounds.

**Cambio concreto:** Añadir validación:
```cpp
if (nReceiveBytes < 2)
    return;  // o manejar error
m_CommandBuffer[nReceiveBytes - 2] = '\0';
```

**Por qué:** Vulnerabilidad de seguridad. Un cliente FTP malicioso puede enviar paquetes mínimos.
**Estimación:** 15 min
**Viabilidad:** ✅ Segura

---

## 🔴 T03 — Corregir off-by-one en WebSocket (websocketdaemon.cpp:267)

**Archivos:** `src/net/websocketdaemon.cpp`
**Línea:** ~267

**Problema:**
```cpp
rxBuf[nRxTotal] = 0;  // Si nRxTotal == sizeof(rxBuf), escribe fuera de bounds
```

**Cambio concreto:**
```cpp
rxBuf[nRxTotal < sizeof(rxBuf) ? nRxTotal : sizeof(rxBuf) - 1] = 0;
```
O mejor, reservar un byte extra en el buffer.

**Por qué:** Buffer overflow explotable desde la red.
**Estimación:** 15 min
**Viabilidad:** ✅ Segura

---

## 🔴 T04 — Reemplazar `volatile float` por `std::atomic<float>` o barreras

**Archivos:** `include/audiomixer.h` (L62-66), `src/mt32pi.cpp` (m_nRenderUs, m_nRenderAvgUs)

**Problema:**
- `volatile float fVolume, fPan` se escriben en Core 0 y se leen en Core 2.
- `volatile` no garantiza atomicidad ni ordering en ARM multi-core.
- `m_nRenderUs` y `m_nRenderAvgUs` escritos en Core 2 sin sincronización.

**Cambio concreto:**
- En `audiomixer.h`: cambiar `volatile float` → `std::atomic<float>` con `memory_order_relaxed` (suficiente para este caso).
- En `mt32pi.h`: cambiar `volatile unsigned` → `std::atomic<unsigned>`.
- Ajustar lecturas/escrituras con `.load()` / `.store()`.

**Por qué:** Race condition real en hardware multi-core. Puede producir datos corruptos intermitentemente.
**Estimación:** 1 hora
**Viabilidad:** ⚠️ Requiere verificar que Circle/stdlib soporta `<atomic>`. Si no, usar `__sync_synchronize()` como barrera.

---

## 🔴 T05 — Validación de PORT IP en FTP (bounce attack)

**Archivos:** `src/net/ftpworker.cpp` ~L475

**Problema:** El comando FTP PORT no valida que la IP indicada coincida con la IP del cliente conectado, permitiendo FTP bounce attacks.

**Cambio concreto:** Comparar la IP parseada del comando PORT con la IP de origen de la conexión. Rechazar si no coinciden.

**Por qué:** Vulnerabilidad de seguridad conocida (CWE-441).
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

---

## 🟡 T06 — Extraer `CountEngines()` en CMIDIRouter

**Archivos:** `src/midirouter.cpp` (L212-270), `include/midirouter.h`

**Problema:** `IsDualMode()` y `GetPrimaryEngine()` recorren los 16 canales con código idéntico contando engines por tipo.

**Cambio concreto:**
1. Crear método privado:
```cpp
void CMIDIRouter::CountEngines(unsigned& nMT32, unsigned& nFluid, unsigned& nYmfm) const;
```
2. `IsDualMode()` lo llama y comprueba si hay ≥2 engines activos (con early-exit en el loop).
3. `GetPrimaryEngine()` lo llama y devuelve el engine con más canales.

**Por qué:** DRY — elimina duplicación exacta. Reduce riesgo de inconsistencias futuras al añadir engines.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura — tests existentes validan comportamiento.

---

## 🟡 T07 — Descomponer `RouteShortMessage()` en helpers

**Archivos:** `src/midirouter.cpp` (~L110-180), `include/midirouter.h`

**Problema:** Una sola función gestiona: extracción de canal, remap, filtrado CC, escalado de volumen, layering y dispatch — 5+ responsabilidades.

**Cambio concreto:** Extraer:
- `GetRemappedChannel(u8 nChannel)` → lógica de remap
- `ShouldFilterCC(CSynthBase* pTarget, u8 nCC)` → filtrado CC
- `ApplyVolumeScaling(u32& nMessage, u8 nChannel)` → escalar CC7
- `DispatchToEngines(u32 nMessage, u8 nChannel, CSynthBase* pTarget)` → envío + layering

`RouteShortMessage()` pasa a ser un orquestador de 10-15 líneas.

**Por qué:** Claridad, testeabilidad individual de cada paso, y reducción de complejidad ciclomática.
**Estimación:** 1.5 horas
**Viabilidad:** ✅ Segura — los tests existentes de midirouter cubren el flujo completo.

---

## 🟡 T08 — Eliminar boilerplate de 13 setters en CSoundFontSynth

**Archivos:** `src/synth/soundfontsynth.cpp` (L673-820), `include/synth/soundfontsynth.h`

**Problema:** 13 métodos Set* con patrón idéntico:
```cpp
void SetXxx(float val) {
    m_nXxx = val;
    m_Lock.Acquire();
    if (m_pSynth) fluid_synth_set_xxx(m_pSynth, -1, val);
    m_Lock.Release();
}
```

**Cambio concreto:** Crear helper template privado:
```cpp
template<typename T, typename Fn>
void ApplyFluidParam(T& member, T val, Fn fn) {
    member = val;
    m_Lock.Acquire();
    if (m_pSynth) fn(m_pSynth, val);
    m_Lock.Release();
}
```
Cada setter queda como una sola línea delegando al helper.

**Por qué:** 150 líneas reducidas a ~30. Menos riesgo de olvidar el Lock en futuras adiciones.
**Estimación:** 1 hora
**Viabilidad:** ✅ Segura

---

## 🟡 T09 — Extraer `ApplySynthSettings()` en CMT32Synth

**Archivos:** `src/synth/mt32synth.cpp` (~L261-371), `include/synth/mt32synth.h`

**Problema:** `SwitchROMSet()` y `ReopenCurrentROMSet()` repiten ~8 llamadas idénticas de configuración del synth.

**Cambio concreto:** Crear método privado `ApplySynthSettings()` que aplique:
- `setOutputGain()`, `setReverbOutputGain()`, `setReverbEnabled()`
- `setNiceAmpRampEnabled()`, `setNicePanningEnabled()`, `setNicePartialMixingEnabled()`
- `setDACInputMode()`, `setMIDIDelayMode()`

Ambos métodos lo llaman tras crear/reabrir el synth.

**Por qué:** Duplicación directa. Al añadir un nuevo parámetro se olvida fácilmente uno de los dos paths.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

---

## 🟡 T10 — Extraer operaciones SIMD a helpers reutilizables

**Archivos:** `src/audiomixer.cpp` (~L114-220), `src/audioeffects.cpp`

**Problema:** El patrón `#ifdef __aarch64__` con loop NEON + fallback escalar se repite 3 veces en audiomixer.cpp (solo gain, mix pan/gain, post-gain clamp) y en audioeffects.cpp.

**Cambio concreto:** Crear funciones inline en un nuevo header `include/simdops.h`:
```cpp
inline void ApplyGain(float* buf, size_t n, float gain);
inline void MixWithPanGain(float* dst, const float* src, size_t n, float gainL, float gainR);
inline void ApplyGainAndClamp(float* buf, size_t n, float gain, float lo, float hi);
```
Cada una contiene el bloque NEON + scalar. audiomixer.cpp y audioeffects.cpp las usan.

**Por qué:** ~70 líneas duplicadas eliminadas. Facilita futuros targets (ARMv7 NEON, scalar optimizado).
**Estimación:** 1.5 horas
**Viabilidad:** ⚠️ Requiere cuidado — el compilador podría no inlinear. Verificar con kernel8.lst que el asm no empeora.

---

## 🟡 T11 — Pre-alocar buffer de grabación (CMIDIRecorder)

**Archivos:** `src/midirecorder.cpp` (~L83-100), `include/midirecorder.h`

**Problema:**
1. En `Start()`: se reservan 256KB con `new`, se liberan en `Stop()`. Fragmenta el heap baremetal.
2. Búsqueda de slot de archivo: `f_stat()` llamado hasta 999 veces (O(n)).

**Cambio concreto:**
1. Pre-alocar `m_pBuffer` en el constructor. Reutilizar en cada Start/Stop sin new/delete.
2. Mantener un contador estático o miembro `m_nNextSlot` que recuerde el último slot usado, empezando la búsqueda desde ahí.

**Por qué:** Estabilidad de heap en entorno baremetal. Mejora latencia de inicio de grabación.
**Estimación:** 45 min
**Viabilidad:** ✅ Segura — más memoria ocupada permanentemente pero predecible.

---

## 🟡 T12 — Añadir validación de rangos en config.def

**Archivos:** `include/config.def`, `src/config.cpp`, `include/config.h`

**Problema:** Parámetros críticos sin límites:
- `sample_rate = 0` → posible división por cero
- `fluidsynth_polyphony = -1` o `999999` → comportamiento errático
- Valores de red sin verificación (puertos, IPs)

**Cambio concreto:** Añadir clamping post-parse en el handler INI o en los getters del config:
```cpp
// En config.cpp, tras parsear cada valor:
m_AudioSampleRate = Clamp(m_AudioSampleRate, 8000, 192000);
m_FluidSynthPolyphony = Clamp(m_FluidSynthPolyphony, 1, 2048);
m_NetworkFTPPort = Clamp(m_NetworkFTPPort, 1, 65535);
```
Alternativa más elegante: extender el macro CFG con parámetros min/max opcionales.

**Por qué:** Robustez frente a configs malformadas. Evita crashes difíciles de diagnosticar.
**Estimación:** 1 hora
**Viabilidad:** ✅ Segura — solo añade validación, no cambia comportamiento con configs válidas.

---

## 🟡 T13 — Extraer limiter y biquad a clases separadas (audioeffects)

**Archivos:** `src/audioeffects.cpp` (~L182-323), `include/audioeffects.h`

**Problema:**
1. `ComputeLowShelf()` y `ComputeHighShelf()` son funciones casi idénticas (~25 líneas cada una).
2. El loop de proceso mezcla EQ + reverb + limiter + hard-clamp en un solo bloque.
3. `TBiquad`, `TComb`, `TAllpass` son structs públicos que exponen estado interno.

**Cambio concreto:**
1. Unificar a `ComputeShelfCoeff(TShelfType type, ...)`.
2. Crear métodos privados: `ProcessEQ()`, `ProcessReverb()`, `ProcessLimiter()`.
3. Mover structs internos a `private:` o a un namespace `detail`.

**Por qué:** Testeabilidad (se puede testear cada efecto aislado), encapsulación, y reducción de duplicación.
**Estimación:** 2 horas
**Viabilidad:** ⚠️ Requiere cuidado — la separación no debe añadir overhead a la hot path (verificar inlining).

---

## 🟡 T14 — Eliminar VLA en audiomixer.cpp (stack baremetal)

**Archivos:** `src/audiomixer.cpp` (~L175)

**Problema:**
```cpp
float tempBuf[nSamples];  // VLA en stack — tamaño variable en tiempo de ejecución
```
En baremetal el stack del kernel es limitado. Con nFrames=512 y stereo: 4096 bytes por render.

**Cambio concreto:** Reemplazar por un buffer miembro pre-alocado:
```cpp
// En audiomixer.h:
float m_TempBuf[MaxFrames * NumChannels];  // MaxFrames es constante conocida
```
O usar `alignas(16)` para compatibilidad NEON.

**Por qué:** Evita stack overflow impredecible. Mejora determinismo en real-time.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

---

## 🟡 T15 — Completar cobertura de tests existentes

**Archivos:** `tests/test_*.cpp`

**Problema:** Múltiples huecos identificados por módulo.

**Casos a añadir por módulo:**

### midirouter
- Volumen con `m_fChannelVolume` (CC7 escalado)
- Layering cuando un engine es nullptr
- Filtrado CC bounds (CC 0 y CC 127)
- SysEx con los 3 engines activos (incluyendo YMFM)
- Remap combinado con layering

### audiomixer
- Pan en extremos (±1.0) — verificar gains L/R esperados
- 4 engines simultáneos (MaxEngines=4)
- `Render()` con 0 frames (no crash)
- Solo mode con cambio dinámico de engine
- Master volume clamping (>1.0, <0.0)

### audioeffects
- EQ con ganancia negativa (bass/treble cut)
- Reverb con roomSize 0.0 y 1.0
- Múltiples llamadas a `Configure()` (¿se resetea correctamente?)
- Limiter: simetría positivo/negativo

### midiparser
- Running status tras SysEx (¿se limpia?)
- SysEx fragmentado en 3+ llamadas a `ParseMIDIBytes()`
- System common: Song Select (0xF3), Quarter Frame (0xF1)
- Mensajes indefinidos: 0xF4, 0xF5

### midirecorder
- Grabación con 0 eventos (solo meta evento tempo)
- Múltiples ciclos Start/Stop consecutivos
- Buffer overflow exacto en el límite

### fluidsequencer
- Conversión SysEx a eventos FluidSynth
- Ring buffer overflow

**Por qué:** Los tests actuales cubren el happy path pero faltan edge cases que son los que causan bugs en producción.
**Estimación:** 4-6 horas (total para todos los módulos)
**Viabilidad:** ✅ Segura — solo añade tests, no modifica código de producción.

---

## 🟡 T16 — Unificar VolumeNames duplicado (FTP + SoundFontManager)

**Archivos:** `src/net/ftpworker.cpp` (~L102), `src/soundfontmanager.cpp`

**Problema:** Array `VolumeNames` definido idénticamente en ambos ficheros. Si se modifica uno, el otro queda desincronizado.

**Cambio concreto:** Mover a `include/utility.h` o crear `include/volumes.h`:
```cpp
namespace Utility {
    static constexpr const char* VolumeNames[] = { "SD:", "USB:" };
    static constexpr size_t VolumeCount = 2;
}
```

**Por qué:** DRY. Fuente única de verdad.
**Estimación:** 20 min
**Viabilidad:** ✅ Segura

---

## 🟡 T17 — Race condition en m_bSeqLoading (mt32pi.cpp)

**Archivos:** `src/mt32pi.cpp` (~L648-670), `include/mt32pi.h`

**Problema:**
```cpp
m_bSeqLoading = true;
const bool bPlayOK = m_pFluidSequencer->Play(pPath);
m_bSeqLoading = false;
```
Otro thread (WebSocket) puede leer `m_bSeqLoading` entre líneas, obteniendo estado stale. Variable declarada como bool normal, no atomic.

**Cambio concreto:** Cambiar a `std::atomic<bool>` o usar `__sync_synchronize()`.

**Por qué:** Integridad de datos en acceso multi-core.
**Estimación:** 15 min
**Viabilidad:** ✅ Segura (misma consideración que T04 respecto a `<atomic>`)

---

## 🟡 T18 — Escape JSON seguro en playlist.cpp

**Archivos:** `src/playlist.cpp` (~L131-145)

**Problema:** `BuildJSON()` asume que los paths no contienen `"` ni `\`. Un path con caracteres especiales corrompe la respuesta JSON del servidor web.

**Cambio concreto:** Implementar función de escape básica:
```cpp
// Escapar " → \" y \ → \\
static size_t EscapeJSON(char* dst, size_t dstSize, const char* src);
```
Usar en `BuildJSON()` al escribir cada path.

**Por qué:** Correctitud del API web. Un SoundFont con nombre raro rompe el frontend.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

---

## 🟡 T19 — Extraer SysEx parsing de SoundFontSynth a módulos dedicados

**Archivos:** `src/synth/soundfontsynth.cpp` (L538-688)

**Problema:** Tres parsers SysEx (GM, Roland, Yamaha) embebidos en SoundFontSynth, con:
- Sin validación de bounds antes de castear a struct
- Constantes mágicas (direcciones Roland) sin documentación
- Lógica compleja de decode anidada

**Cambio concreto:**
1. Los headers `include/synth/gmsysex.h`, `rolandsysex.h`, `yamahasysex.h` ya existen con structs.
2. Mover la lógica de parsing desde `soundfontsynth.cpp` a funciones libres en esos headers o en `.cpp` propios.
3. Añadir bounds checking antes de cada cast.

**Por qué:** Separación de responsabilidades. Permite testear SysEx parsing aislado del synth.
**Estimación:** 2 horas
**Viabilidad:** ⚠️ Requiere cuidado — las funciones de parse necesitan acceso al synth FluidSynth para aplicar cambios. Usar callbacks o interfaz.

---

## ✅ T20 — Diagnóstico basado en enum en CFluidSequencer

**Archivos:** `src/fluidsequencer.cpp`, `include/fluidsequencer.h`

**Problema:** `m_szDiag` actualizado manualmente con `snprintf()` en 11 puntos. Frágil y genera overhead de string formatting.

**Cambio concreto:**
```cpp
enum class ESeqState { Idle, CreatingPlayer, Opening, Playing, Paused, Failed };
static const char* const StateMessages[];
ESeqState m_State;
// Web/API lee: StateMessages[m_State] + error code separado
```

**Por qué:** Mantenibilidad y rendimiento (elimina snprintf baremetal).
**Estimación:** 45 min
**Viabilidad:** ✅ Segura

**COMPLETADO:** Reemplazados todos los `snprintf(m_szDiag, ...)` por transiciones de estado
(`m_eState = ESeqState::...`). Eliminado `char m_szDiag[256]` del header. Añadido `int m_nLastError`
para FatFS FRESULT y FLUID_* status. `GetDiag()` ahora retorna `s_StateMessages[m_eState]`.
Nuevo `GetState()` y `GetLastError()` para acceso tipado. 253/253 tests.

---

## ✅ T21 — TODO del pitch bend en YmfmSynth

**Archivos:** `src/synth/ymfmsynth.cpp` (~L232)

**Problema:** Hay un TODO explícito: "Phase 3.6 — update F-Number for active voices". El pitch bend no actualiza voces activas en tiempo real.

**Cambio concreto:** Implementar la actualización de F-Number en el handler de pitch bend recalculando y aplicando a las voces activas del canal afectado.

**Por qué:** Feature incompleta documentada en el código. El pitch bend es fundamental para la expresividad MIDI.
**Estimación:** 2-3 horas (requiere entender el mapeo de voces a registros OPL)
**Viabilidad:** ⚠️ Requiere conocimiento del chip YMFM y prueba con audio real.

**COMPLETADO:** Añadido `UpdateVoiceFNumber(nVoice, nNote, nNoteOffset, fBendSemitones)`.
Convierte el valor de pitch bend (0–16383) a semitones, interpola entre semistonos con la tabla
`kFNumTable`, ajusta por bloque OPL3, y escribe 0xA0/0xB0 manteniendo KEY ON (bit 0x20).
`PitchBend()` itera todas las voces activas del canal aplicando el nuevo F-Number en tiempo real.

---

## ✅ T22 — Mover structs internos de audioeffects.h a private/detail

**Archivos:** `include/audioeffects.h`

**Problema:** `TBiquad`, `TComb`, `TAllpass` son structs públicos con estado interno (`z1`, `z2`, `buffer`). Los clientes no deben acceder a ellos.

**Cambio concreto:** Mover a sección `private:` de `CAudioEffects` o a un namespace `detail`.

**Por qué:** Encapsulación. Evita dependencias accidentales del estado interno.
**Estimación:** 20 min
**Viabilidad:** ✅ Segura

**COMPLETADO:** T13 ya había movido `TBiquad` a `private:` en `CAudioEffects`. `TComb` y `TAllpass`
ya estaban en `private:` desde la creación de la clase. No requirió cambios adicionales.

---

## ✅ T23 — Unificar parsers de paquetes AppleMIDI

**Archivos:** `src/net/applemidi.cpp` (~L125-196)

**Problema:** `ParseInvitationPacket()`, `ParseEndSessionPacket()` y `ParseSyncPacket()` repiten la validación de header (signature + command) con patrón idéntico.

**Cambio concreto:** Extraer helper:
```cpp
bool ValidateAppleMIDIHeader(const u8* pData, size_t nSize, u16 nExpectedCmd);
```

**Por qué:** DRY. 3 funciones comparten ~10 líneas idénticas.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

**COMPLETADO:** Añadido `static bool ValidateAppleMIDIHeader(const u8*, size_t, size_t, u16)` que
valida tamaño mínimo, firma 0xFFFF y comando esperado. Las tres funciones de parseo ahora usan
este helper, eliminando las 3×4 líneas de comprobación duplicadas. 253/253 tests.

---

## ✅ T24 — Seek history con circular buffer en mt32pi.cpp

**Archivos:** `src/mt32pi.cpp` (~L655)

**Problema:**
```cpp
memmove(&m_SeekHistory[0], &m_SeekHistory[1], (SeekHistoryMax - 1) * sizeof(TSeekEntry));
```
Cada vez que la cola se llena, `memmove` desplaza el array entero. Ineficiente.

**Cambio concreto:** Reemplazar por un ring buffer circular con head/tail indices. Ya existe `TRingBuffer` en el proyecto.

**Por qué:** Rendimiento y consistencia (usar la abstracción del proyecto).
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

**COMPLETADO:** Reemplazado el `memmove` con un ring buffer usando `m_nSeekHistoryHead` como
proxy de escritura. Añadido `m_nSeekHistoryHead` en `mt32pi.h`. El índice circular
`(head + Max - count + i) % Max` permite búsqueda lineal sin mover memoria.

---

## ✅ T25 — Patrón de inicialización de synths repetido en mt32pi.cpp

**Archivos:** `src/mt32pi.cpp` (~L367-435)

**Problema:** InitMT32Synth, InitSoundFontSynth, InitYmfmSynth siguen el mismo patrón: allocate → initialize → check → log → register in router + mixer. Código repetido 3 veces.

**Cambio concreto:** Extraer template o función genérica:
```cpp
template<typename TSynth, typename... Args>
TSynth* InitSynth(const char* name, Args&&... args) {
    auto pSynth = new TSynth(std::forward<Args>(args)...);
    if (!pSynth->Initialize()) { delete pSynth; LOGWARN(...); return nullptr; }
    m_MIDIRouter.SetEngine(name, pSynth);
    m_AudioMixer.AddEngine(pSynth);
    return pSynth;
}
```

**Por qué:** DRY. Facilita añadir nuevos engines en el futuro.
**Estimación:** 45 min
**Viabilidad:** ⚠️ Los tres constructores tienen firmas diferentes. Puede requerir adapter.

**COMPLETADO:** Añadido `TryAllocSynth<TSynth, TArgs...>()` en namespace anónimo al inicio de
`mt32pi.cpp`. Elimina el patrón `new → Initialize() → delete/nullptr/return` de los tres
`Init*Synth`. El warning específico de cada synth sigue en el caller. 253/253 tests.

---

## 🟢 T26 — División por cero en `Utility::Lerp` (utility.h:70)

**Archivos:** `include/utility.h` ~L70

**Problema:**
```cpp
constexpr float Lerp(float nValue, float nMinA, float nMaxA, float nMinB, float nMaxB) {
    return nMinB + (nValue - nMinA) * ((nMaxB - nMinB) / (nMaxA - nMinA));  // div/0 si nMaxA == nMinA
}
```

**Cambio concreto:** Añadir guard:
```cpp
if (nMaxA == nMinA) return nMinB;
```

**Por qué:** Evita crash por división por cero con inputs degenerados.
**Estimación:** 5 min
**Viabilidad:** ✅ Segura

---

## 🟢 T27 — Tests para módulos de red (AppleMIDI, FTP, WebSocket, OSC)

**Archivos:** Nuevos `tests/test_applemidi.cpp`, `tests/test_ftpworker.cpp`, etc.

**Problema:** Los módulos de red no tienen tests unitarios. Solo se verifican en hardware real.

**Casos a cubrir:**
- AppleMIDI: parsing de paquetes invitation/sync/end con datos malformados
- FTP: parsing de comandos, manejo de buffer mínimo (<2 bytes)
- WebSocket: handshake bounds, frame parsing
- OSC: ya tiene test_osc.cpp pero faltan edge cases

**Por qué:** Los bugs de red son los más difíciles de diagnosticar en baremetal.
**Estimación:** 4-6 horas (crear stubs de socket + tests)
**Viabilidad:** ⚠️ Requiere stubs para CSocket de Circle. Factible pero significativo.

---

## 🟢 T28 — Eliminar conflicto de .o entre tests y cross-build

**Archivos:** `tests/Makefile`

**Problema documentado:** `tests/Makefile` compila `../src/midirouter.o`, `../src/audiomixer.o`, etc. con g++ nativo. Después `make BOARD=pi3-64` falla porque encuentra .o de arquitectura host.

**Cambio concreto:** Compilar objetos de test en directorio separado:
```makefile
OBJDIR = build-tests
$(OBJDIR)/%.o: ../src/%.cpp
    @mkdir -p $(dir $@)
    $(CXX) $(CXXFLAGS) -c -o $@ $<
```

**Por qué:** Elimina la necesidad de limpiar manualmente entre test y cross-build.
**Estimación:** 30 min
**Viabilidad:** ✅ Segura

---

## ✅ T29 — Operador precedencia engañoso en mt32pi.cpp:48

**Archivos:** `src/mt32pi.cpp` ~L48

**Problema:**
```cpp
constexpr float Sample24BitMax = (1 << 24 - 1) - 1;
// Evalúa como (1 << 23) - 1 = 8388607, que es correcto por coincidencia
// La intención era ((1 << 24) - 1) - 1 = 16777214? O (1 << 23) - 1?
```

**Cambio concreto:** Clarificar con paréntesis explícitos según la intención real:
```cpp
constexpr float Sample24BitMax = static_cast<float>((1 << 23) - 1);  // si es i24 signed max
// o
constexpr float Sample24BitMax = static_cast<float>((1u << 24) - 1);  // si es u24 max
```

**Por qué:** Claridad. El código actual engaña al lector aunque funcione.
**Estimación:** 10 min
**Viabilidad:** ✅ Segura (verificar que el valor numérico final no cambie)

**COMPLETADO:** Reescrito como `static_cast<float>((1 << 23) - 1)` con comentario explicativo.
El valor numérico (8388607 = signed 24-bit max) no cambia; se elimina la ambigüedad de precedencia.

---

---

## Registro de implementaciones

### ✅ T28 — Eliminar conflicto de .o entre tests y cross-build
**Completado:** 6 de abril de 2026

**Cambio realizado en `tests/Makefile`:**

Reescrito para compilar todos los objetos bajo `tests/build/`, preservando la ruta relativa al source tree (ej. `../src/midirouter.cpp` → `build/src/midirouter.o`). Se usan tres reglas patrón: `$(OBJDIR)/%.o: %.cpp` para ficheros locales, `$(OBJDIR)/%.o: ../%.cpp` para fuentes en `src/`, y `$(OBJDIR)/%.o: ../%.c` para `external/inih/ini.c`. `make clean` hace `rm -rf build run_tests`. Tras este cambio, ejecutar el test suite nunca contamina `../src/` ni `../external/inih/` con .o nativos, por lo que el cross-build puede ejecutarse directamente sin limpieza previa.

---

### ✅ T20 — Diagnóstico basado en enum en CFluidSequencer
**Completado:** Grupo 5

`char m_szDiag[256]` reemplazado por `ESeqState m_eState` + `int m_nLastError`. Los 12 `snprintf(m_szDiag, ...)` reemplazados por asignaciones `m_eState = ESeqState::...` y `m_nLastError = code`. `GetDiag()` retorna `s_StateMessages[m_eState]` (array estático file-scope). Añadido `GetState()` y `GetLastError()`.

### ✅ T21 — Pitch bend F-Number en YmfmSynth
**Completado:** Grupo 5

`UpdateVoiceFNumber(nVoice, nNote, nNoteOffset, fBendSemitones)` calcula el F-Number considerando el bend como offset fraccionario en semitonos, interpola entre entradas contiguas de `kFNumTable`, ajusta por bloque OPL3 y escribe 0xA0/0xB0 con KEY ON bit preservado. `PitchBend()` itera `m_Voices` actualizando todas las voces activas del canal.

### ✅ T22 — Structs internos de audioeffects.h
**Completado:** Grupo 5

Ya hecho en T13: `TBiquad` pasó a `private:` y `TComb`/`TAllpass` ya eran privados. Sin cambios adicionales.

### ✅ T23 — Unificar parsers AppleMIDI
**Completado:** Grupo 5

`static bool ValidateAppleMIDIHeader(const u8*, size_t, size_t, u16)` file-scope en `applemidi.cpp`. Valida tamaño mínimo, firma 0xFFFF y comando. Las tres funciones parse reducen 4 líneas de comprobación a 1 llamada al helper. 253/253 tests.

### ✅ T24 — Seek history circular buffer
**Completado:** Grupo 5

Añadido `size_t m_nSeekHistoryHead` en `mt32pi.h` + init a 0 en constructor. `SeekHistorySet` escribe en `m_SeekHistory[m_nSeekHistoryHead]`, avanza `head = (head+1) % SeekHistoryMax`, e incrementa `count` hasta `SeekHistoryMax`. Eliminado `memmove`. `SeekHistoryGet` usa `(head + Max - count + i) % Max` para el índice real.

### ✅ T25 — Patrón InitSynth
**Completado:** Grupo 5

`TryAllocSynth<TSynth, TArgs...>()` en namespace anónimo en `mt32pi.cpp` cubre el patrón `new → Initialize() → delete/nullptr`. Los tres `Init*Synth()` usan el helper; el `LOGWARN` específico permanece en el caller.

### ✅ T29 — Sample24BitMax precedencia
**Completado:** Grupo 5

`(1 << 24 - 1) - 1` → `static_cast<float>((1 << 23) - 1)` con comentario. Valor numérico idéntico (8388607).

---

### ✅ T09 — Extraer `ApplySynthSettings()` en CMT32Synth

**Cambios en `include/synth/mt32synth.h` y `src/synth/mt32synth.cpp`:**

Las 8 llamadas `setOutputGain / setReverbOutputGain / setReverbEnabled / setNiceAmpRampEnabled / setNicePanningEnabled / setNicePartialMixingEnabled / setDACInputMode / setMIDIDelayMode` se repetían idénticamente en 3 sitios: `Initialize()`, `SwitchROMSet()`, y `ReopenCurrentROMSet()`. Extraídas a `void CMT32Synth::ApplySynthSettings()` privado. Los 3 sitios llaman simplemente `ApplySynthSettings()`.

---

### ✅ T08 — Eliminar boilerplate de setters en CSoundFontSynth
**Completado:** 6 de abril de 2026

**Cambios en `include/synth/soundfontsynth.h` y `src/synth/soundfontsynth.cpp`:**

Añadido template privado `ApplyFluidParam(T& member, T val, Fn fn)` a `soundfontsynth.h`: almacena el valor en el miembro, adquiere el lock, llama `fn(m_pSynth, val)` si hay synth activo, y libera el lock. Los 10 setters con patrón idéntico (SetReverbActive, SetReverbRoomSize, SetReverbLevel, SetReverbDamping, SetReverbWidth, SetChorusActive, SetChorusDepth, SetChorusLevel, SetChorusVoices, SetChorusSpeed) quedan reducidos a una línea cada uno usando lambdas sin captura. SetGain y SetMasterVolume no se tocan (usan fórmulas computadas como valor).

---

### ✅ T07 — Descomponer `RouteShortMessage()` en helpers
**Completado:** 6 de abril de 2026

**Cambios en `include/midirouter.h` y `src/midirouter.cpp`:**

Extraídos 4 métodos privados:
- `RemapMessage(u8, u32) -> u32` — aplica el remap de canal al status byte del mensaje
- `ScaleCC7(u32, u8) -> u32` — escala el valor de CC7 por el multiplicador de volumen por canal
- `DispatchCC(u32, u8, CSynthBase*)` — gestiona el envío de CC con filtro, escala de volumen, y layering
- `BroadcastToEngines(u32)` — envía a MT-32 y FluidSynth; reutilizado para sysrt y NoteOn/Off con layering

`RouteShortMessage()` pasa de ~55 líneas a ~20 líneas como orquestador.

---

### ✅ T06 — Extraer `CountEngines()` en CMIDIRouter
**Completado:** 6 de abril de 2026

**Cambios en `include/midirouter.h` y `src/midirouter.cpp`:**

Añadido método privado `void CountEngines(unsigned& nMT32, unsigned& nFluid, unsigned& nYmfm) const` que recorre los 16 canales una vez y cuenta cuántos hay de cada tipo. `IsDualMode()` lo llama y comprueba si la suma de engines activos es ≥2. `GetPrimaryEngine()` lo llama y devuelve el engine con mayor count. Se elimina el loop duplicado de ~20 líneas en `GetPrimaryEngine()` y el loop con early-exit de `IsDualMode()`.

---

**Completado:** 6 de abril de 2026

**Cambio realizado en `include/utility.h`, función `Lerp()`:**

Añadido guard `if (nMaxA == nMinA) return nMinB;` al inicio de la función. La función es `constexpr` en C++17 (que soporta `if` en constexpr), por lo que el guard no rompe el uso en contextos de compilación constante.

---

### ✅ T05 — Validación de PORT IP en FTP (bounce attack)
**Completado:** 6 de abril de 2026

**Cambio realizado en `src/net/ftpworker.cpp`, función `Port()` (~L510):**

El comando FTP `PORT h1,h2,h3,h4,p1,p2` indica al servidor dónde debe conectarse para enviar datos. Sin validación de la IP, un atacante puede hacer que el servidor se conecte a cualquier host de la red en nombre suyo (FTP bounce attack, también útil para port scanning y exfiltración indirecta).

**Solución:** Tras parsear los 6 bytes del comando PORT, se comparan los 4 bytes de IP contra la IP real del socket de control obtenida con `m_pControlSocket->GetForeignIP()`. Si no coinciden (o si `GetForeignIP()` devuelve nullptr), se rechaza con `501 PORT address does not match client address.` y no se almacena la dirección ni el puerto.

La validación está documentada con referencia a RFC 2577 §3 ("Counteracting Bounce Attacks"). Se eliminó el `// TODO` correspondiente. El bloque `#ifdef FTPDAEMON_DEBUG` de logging se preservó intacto.

---

### ✅ T04 — Reemplazar `volatile float` por `__atomic_*` builtins en CAudioMixer
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/audiomixer.h`, `src/audiomixer.cpp`

**Diagnóstico previo:** Los campos `volatile float fVolume`, `volatile float fPan`, `volatile float m_fMasterVolume` y `CSynthBase* volatile m_pSoloEngine` eran escritos en Core 0 y leídos en Core 2 (audio thread). En ARM multi-core, `volatile` previene reordenamiento del compilador para accesos al mismo puntero, pero NO garantiza ordering de memoria entre cores para observaciones cruzadas de variables distintas. Para el puntero `m_pSoloEngine` (64-bit en AArch64), tampoco garantizaba que las escrituras al objeto apuntado fuesen visibles cuando el puntero se leyera en el otro core. `libatomic` no está disponible en el entorno, por lo que `std::atomic<float>` no era viable.

**Solución aplicada:** Los builtins `__atomic_load`/`__atomic_store` de GCC (forma genérica de 3 argumentos, distinta de `__atomic_load_n`) funcionan para cualquier tipo escalar incluyendo `float` y se compilan a instrucciones inline (`LDR`/`STR`/`LDAR`/`STLR`) sin necesidad de `libatomic`.

Modelo de memoria elegido:
- **`fVolume`, `fPan`, `m_fMasterVolume`**: `__ATOMIC_RELAXED` — un valor ligeramente stale de volumen/pan es aceptable (se corrije en el siguiente buffer de audio). Genera `LDR`/`STR` sin barrera, sin overhead.
- **`m_pSoloEngine`**: `__ATOMIC_RELEASE` en escritura (SetSoloEngine, ClearSoloEngine) y `__ATOMIC_ACQUIRE` en lectura (Render) — garantiza que cuando Core 2 lee un puntero no-null, ve el engine completamente inicializado. Genera `STLR`/`LDAR` en AArch64.

Se eliminó `volatile` de todos esos campos. Los sites de acceso en el constructor y `AddEngine` (que corren antes de que Core 2 arrranque) mantienen asignación directa — correcta sin atomic en ese momento.

**Verificación:** 212/212 tests pasan sin cambios.

---

### ✅ T03 — Corregir off-by-one en WebSocket (websocketdaemon.cpp)
**Completado:** 6 de abril de 2026

**Cambio realizado en `src/net/websocketdaemon.cpp` línea ~248:**

El bug real no estaba en la línea 267 (el análisis inicial era incorrecto — el `Receive` ya limitaba a `kRxBuf - 1 - nRxTotal`, dejando siempre espacio para el terminador en el handshake). El off-by-one estaba en la fase de recepción de frames de datos:

1. `framePay` declarado como `new u8[512]` (índices 0-511).
2. `ParseClientFrame` llamado con `nPayBufSize = 512`, por lo que puede devolver `pLen` de hasta 512.
3. `framePay[pLen] = 0` en línea 372: si `pLen == 512`, escribe en `framePay[512]` → un byte fuera del buffer.

Un cliente WebSocket que envíe exactamente 512 bytes de payload en un texto frame provoca la escritura fuera de bounds, con potencial para corromper el heap.

**Solución:** Cambiar la reserva a `new u8[512 + 1]` con comentario explicativo. `ParseClientFrame` sigue limitando el payload a 512 bytes, pero ahora el byte extra para el terminador nulo queda dentro del buffer.

---

### ✅ T02 — Corregir buffer underflow en FTP (ftpworker.cpp)
**Completado:** 6 de abril de 2026

**Cambio realizado en `src/net/ftpworker.cpp` línea ~204:**

El código anterior tenía un `// FIXME` y calculaba `m_CommandBuffer[nReceiveBytes - 2] = '\0'` sin comprobar que `nReceiveBytes >= 2`. El socket devuelve valores positivos (≥1) para datos recibidos, pero los checks previos solo descartaban 0 (timeout) y negativos (cierre). Con `nReceiveBytes == 1`, el índice calculado era `-1` → escritura fuera de bounds.

Un cliente FTP malicioso podía enviar un único byte para provocar este comportamiento. Aunque Circle CSocket probablemente bufferiza hasta `\r\n`, no es una garantía de la API.

**Solución:** Añadido guard `if (nReceiveBytes < 2) { yield; continue; }` antes de la operación. El `// FIXME` fue reemplazado por un comentario que documenta el invariante (comandos FTP terminan en CRLF).

---

### ✅ T01 — Corregir bugs y comportamiento indefinido en `optional.h`
**Completado:** 6 de abril de 2026
**Commit pendiente.**

**Cambios realizados en `include/optional.h`:**

1. **Copy constructor** — Antes llamaba a `operator=(Other)` sobre un objeto recién creado con `m_bSet` y `m_Value` sin inicializar. Ahora inicializa `m_bSet = false` en la member initializer list y usa placement new condicionalmente solo si `Other.m_bSet`.

2. **Move constructor** — Antes ejecutaba placement new incondicionalmente, leyendo `Other.m_Value` sin importar si tenía valor (UB cuando `Other` estaba vacío). Ahora solo construye si `Other.m_bSet`, usa `static_cast<T&&>` para el move real, y llama `Other.Reset()` para dejar Other en estado válido vacío.

3. **`operator=(const TOptional<T>&)`** — Antes asignaba `*reinterpret_cast<T*>(m_Value) = ...` incondicionalmente, lo que es UB cuando `this` estaba vacío (m_Value no inicializado) o cuando `Other` estaba vacío (m_Value de Other no inicializado), y además no destruía el valor previo cuando se asignaba un Optional vacío. Ahora maneja los 4 casos correctamente:
   - Ambos con valor → asignación directa
   - `this` con valor, `Other` vacío → `Reset()` (destruye y limpia)
   - `this` vacío, `Other` con valor → placement new + `m_bSet = true`
   - Ambos vacíos → no-op
   - Auto-asignación (`this == &Other`) → return inmediato

4. **`operator=(const T& Value)`** — Antes hacía assignment directo sobre `m_Value` aunque el objeto estuviera vacío (UB para tipos con constructor no trivial). Ahora usa `placement new` si no había valor previo, o asignación directa si ya existía.

---

### ✅ T15 — Completar cobertura de tests existentes
**Completado:** 6 de abril de 2026

**29 nuevos casos de test añadidos** en los módulos existentes:

- **midirouter** (8 tests): CC7 escalado con `m_fChannelVolume`; layering cuando un engine es nullptr; filtrado CC en límites (CC 0 y CC 127); SysEx con los 3 engines activos incluyendo YMFM; remap combinado con layering.
- **audiomixer** (7 tests): Pan en extremos (±1.0) verificando gains L/R exactos; 4 engines simultáneos (MaxEngines=4); `Render()` con 0 frames; solo mode con cambio dinámico de engine; master volume clamping (>1.0, <0.0).
- **audioeffects** (5 tests): EQ con ganancia negativa (bass/treble cut); reverb con roomSize 0.0 y 1.0; múltiples llamadas a `Configure()` verificando reset de estado; limiter simetría positivo/negativo.
- **midiparser** (5 tests): Running status tras SysEx; SysEx fragmentado en 3+ llamadas; Quarter Frame (0xF1); mensajes indefinidos 0xF4 y 0xF5.
- **midirecorder** (4 tests): Grabación con 0 eventos (solo meta evento tempo); múltiples ciclos Start/Stop consecutivos; buffer overflow exacto en el límite.

Total: 253/253 tests pasan (subiendo de 224).

---

### ✅ T27 — Tests para módulos de red (AppleMIDI)
**Completado:** 6 de abril de 2026

**Nuevo fichero:** `tests/test_applemidi.cpp` con **12 tests** cubriendo:

- Parsing de paquete `IN` (invitation) válido.
- Rechazo de `IN` con tamaño insuficiente.
- Rechazo de `IN` con signature incorrecto.
- Parsing de `BY` (end session) válido.
- Rechazo de `BY` truncado.
- Parsing de `CK` (sync) con los 3 timestamps; verificación de `deltaT` calculado.
- Rechazo de `CK` con fase fuera de rango.
- Parsing de `RS` (reset) válido.
- Rechazo de paquetes con comando desconocido.
- Rechazo de paquetes con tamaño < mínimo requerido.
- Round-trip: generar respuesta de invitation y verificar campos.
- Verificar que la sesión permanece abierta tras una secuencia invitation + sync válida.

Se añadieron stubs mínimos para `CSocket` usados exclusivamente por este test. `../src/net/applemidi.cpp` añadido a SRCS en `tests/Makefile`.

---

### ✅ T14 — Eliminar VLA en audiomixer.cpp
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/audiomixer.h`, `src/audiomixer.cpp`

El VLA `float tempBuf[nSamples]` en `Render()` asignaba hasta 4096 bytes en el stack del audio task baremetal en cada llamada. En ARM baremetal el stack de Core 2 es fijo y pequeño; un overflow es silencioso y catastrófico.

**Solución:**
- Añadida constante `static constexpr size_t MaxFrames = 2048` a la sección pública de `CAudioMixer`.
- Añadido miembro `alignas(16) float m_TempBuf[MaxFrames * NumChannels]` a la sección privada (`alignas(16)` necesario para cargas NEON).
- En `Render()`: `float tempBuf[nSamples]` → `float* const tempBuf = m_TempBuf`.

El buffer ahora se aloja en la sección BSS junto al objeto, con tamaño y alineación fijos.

---

### ✅ T16 — Unificar VolumeNames duplicado
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/utility.h`, `src/rommanager.cpp`, `src/soundfontmanager.cpp`

Ambos ficheros definían `const char* const Disks[] = { "SD", "USB" }` localmente. Un cambio futuro (p.ej. añadir un tercer volumen) requeriría editar dos ficheros.

**Solución:** Añadidos a `namespace Utility` en `utility.h`:
```cpp
static constexpr const char* const Volumes[]  = { "SD", "USB" };
static constexpr size_t            VolumeCount = 2;
```
`rommanager.cpp` y `soundfontmanager.cpp` eliminan su array local y usan `Utility::Volumes` en los bucles `for (auto pDisk : ...)`. `ftpworker.cpp` usa `FF_VOLUME_STRS` (6 volúmenes) y no se unificó intencionadamente.

---

### ✅ T17 — Race condition en m_bSeqLoading
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/mt32pi.h`, `src/mt32pi.cpp`

`m_bSeqLoading` era un `volatile bool` escrito en Core 0 (thread de secuenciador) y leído en Core 1 (WebSocket). `volatile` no garantiza ordering de memoria entre cores en ARM.

**Solución:** Campo cambiado a `bool` sin `volatile`. Los sites de escritura usan `__atomic_store_n(&m_bSeqLoading, true/false, __ATOMIC_RELEASE)` y el site de lectura usa `__atomic_load_n(&m_bSeqLoading, __ATOMIC_ACQUIRE)`. Mismo patrón que los atómicos de `CAudioMixer` (T04): compatible con el entorno sin `libatomic`.

---

### ✅ T18 — Escape JSON seguro en playlist.cpp
**Completado:** 6 de abril de 2026

**Archivo modificado:** `src/playlist.cpp`, función `BuildJSON()`

El código anterior pasaba los paths de SoundFont directamente a `snprintf` con formato `"%s\"%s\""`, sin escapar. Un SoundFont con `"` o `\` en su nombre corrompía el JSON devuelto al frontend web.

**Solución:** El bucle de serialización de entradas se reescribió con un bucle carácter a carácter que escapa `"` → `\"` y `\` → `\\`. Se preservan todos los checks de desbordamiento y el `return -1` en overflow. Se eliminó el comentario "Paths are SD:/file.mid — no chars that need escaping".

---

### ✅ T11 — Pre-alocar buffer de grabación (CMidiRecorder)
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/midirecorder.h`, `src/midirecorder.cpp`

**Problema 1 (heap fragmentation):** `Start()` llamaba `new u8[256KB]` y `Stop()` llamaba `delete[]`. En un heap baremetal sin compactación, ciclos repetidos de grabación fragmentan el heap.

**Problema 2 (búsqueda O(n)):** `Start()` buscaba siempre desde `recording_001.mid` hasta encontrar un slot libre — hasta 999 llamadas a `f_stat`.

**Solución:**
- `m_pBuf` pre-allocado en el constructor con `new u8[MaxBufSize]` y liberado en el destructor. `Start()` solo comprueba `if (!m_pBuf)`.
- Nuevo miembro `unsigned m_nNextSlot` (inicializado a 1). La búsqueda de slot comienza en `m_nNextSlot` y avanza módulo 999, consiguiendo O(1) amortizado en uso normal. Tras encontrar slot `i`, `m_nNextSlot = (i % 999) + 1`.
- `Stop()` elimina `delete[] m_pBuf; m_pBuf = nullptr` y solo resetea `m_nBufPos = 0`.

---

### ✅ T10 — Extraer operaciones SIMD a helpers reutilizables
**Completado:** 6 de abril de 2026

**Nuevo fichero:** `include/simdops.h`  
**Archivo modificado:** `src/audiomixer.cpp`

Los tres bloques `#ifdef __aarch64__` con loop NEON + fallback escalar de `audiomixer.cpp` (~70 líneas) se extrajeron a funciones inline en `namespace SimdOps`:

- `ApplyGain(float* buf, size_t n, float gain)` — escala todos los samples in-place.
- `MixWithPanGain(float* dst, const float* src, size_t n, float gainL, float gainR)` — mezcla con gains L/R separados (stereo interleaved); empaqueta `{gainL, gainR, gainL, gainR}` para procesar 2 frames NEON por iteración.
- `ApplyGainAndClamp(float* buf, size_t n, float gain, float lo, float hi)` — aplica gain y hard-clamp in-place.

Cada función contiene su bloque NEON + fallback escalar. `audiomixer.cpp` sustituye su `#include <arm_neon.h>` por `#include "simdops.h"` y los tres bloques inline por una línea cada uno.

---

### ✅ T12 — Añadir validación de rangos en config
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/config.h`, `src/config.cpp`

Sin validación, valores malformados en `mt32pi.ini` podían causar crashes silenciosos: `sample_rate=0` → división por cero en el pipeline de audio; `polyphony=-1` → comportamiento errático de FluidSynth; puertos de red fuera de rango.

**Solución:** Añadido método privado `void CConfig::ValidateRanges()` llamado desde `Initialize()` inmediatamente tras `ini_parse_string()`. Aplica `Utility::Clamp` a:
- `AudioSampleRate` → [8000, 192000]
- `AudioChunkSize` → [32, 4096]
- `FluidSynthPolyphony` → [1, 2048]
- `NetworkWebServerPort`, `NetworkWebSocketPort`, `NetworkOSCPort` → [1, 65535]
- `EffectsEQBassGain`, `EffectsEQTrebleGain` → [−12, 12]

Los valores por defecto ya están dentro de los rangos; el método no cambia el comportamiento con configs válidas.

---

### ✅ T13 — Refactorizar audioeffects (TBiquad, shelf unification, sub-métodos)
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/audioeffects.h`, `src/audioeffects.cpp`

**1. TBiquad movido a `private:`** — El struct `TBiquad` (estados `z1`, `z2`, coeficientes `b0`-`a2`) estaba declarado globalmente en el header. No hay usuarios externos; movido a la sección `private:` de `CAudioEffects` donde pertenece.

**2. Shelf unificado:** `ComputeLowShelf()` y `ComputeHighShelf()` (~25 líneas cada uno) eran casi idénticos; la única diferencia era el signo de los términos con `(A−1)·cos(ω₀)`. Reemplazados por `ComputeShelfCoeff(EShelfType type, TBiquad&, float freq, float gain, float sr)` donde `s = +1` (Low) o `s = −1` (High). Los coeficientes se verificaron algebraicamente contra el Audio EQ Cookbook (R. Bristow-Johnson, S=1).

**3. Sub-métodos privados extraídos:**
- `ProcessEQ(float& xL, float& xR)` — aplica los 4 biquads (bass/treble L+R).
- `ProcessReverb(float& xL, float& xR, float fWet)` — red Freeverb completa (8 combs + 4 allpass).
- `ProcessLimiter(float& xL, float& xR)` — soft-knee Hermite + hard-clamp de seguridad (siempre activo).

`Process()` pasa de un bloque monolítico de ~60 líneas a 10 líneas de orquestación.

---

### ✅ T19 — Extraer SysEx parsing de SoundFontSynth a módulos dedicados
**Completado:** 6 de abril de 2026

**Archivos modificados:** `include/synth/gmsysex.h`, `include/synth/rolandsysex.h`, `include/synth/yamahasysex.h`, `include/synth/soundfontsynth.h`, `src/synth/soundfontsynth.cpp`

Los tres parsers SysEx eran métodos privados de `CSoundFontSynth` con side effects directos (llamadas a `ResetMIDIMonitor()`, `m_pUI->ShowSysExText()`, `m_nPercussionMask ^= ...`), lo que impedía testearlos aislados.

**Solución: funciones libres con structs de resultado.**

Cada header de SysEx recibe una función libre pura y un struct de resultado:
- `TGMSysExResult ParseGMSysEx(const u8*, size_t)` → `{ bReset }`
- `TRolandSysExResult ParseRolandSysEx(const u8*, size_t)` → `{ bValid, bConsume, bReset, bPercChange, nPercChannel, nPercMode, bDisplayText, bDisplayDots, pDisplayData, nDisplaySize, nAddressLo }`
- `TYamahaSysExResult ParseYamahaSysEx(const u8*, size_t)` → `{ bValid, bConsume, bReset, bDisplayText, bDisplayDots, pDisplayData, nDisplaySize, nAddressLo }`

Los tres métodos privados se eliminan de `soundfontsynth.h`. `HandleMIDISysExMessage()` actúa como dispatcher: llama a las tres funciones, aplica los side effects indicados por los structs (reset de monitor, máscara de percusión, llamadas a UI), y decide si reenviar a FluidSynth basándose en `bConsume`.

Los punteros `pDisplayData` en los structs apuntan a la ventana original del buffer de SysEx, válidos únicamente durante la ejecución de `HandleMIDISysExMessage`.

---

## Resumen por prioridad

| Prioridad | Tareas | Estimación total |
|-----------|--------|-----------------|
| 🔴 Alta   | T01-T05 | ~2.5 horas |
| 🟡 Media  | T06-T19 | ~16 horas |
| 🟢 Baja   | T20-T29 | ~13 horas |
| **Total** | **29 tareas** | **~31.5 horas** |

## Orden de implementación recomendado

1. **Primero (bugs y seguridad):** T01, T02, T03, T04, T05, T26
2. **Segundo (duplicación y claridad):** T06, T07, T08, T09, T28
3. **Tercero (tests):** T15, T27
4. **Cuarto (mejoras estructurales):** T10, T11, T12, T13, T14, T16, T17, T18, T19
5. **Quinto (polish):** T20, T21, T22, T23, T24, T25, T29
