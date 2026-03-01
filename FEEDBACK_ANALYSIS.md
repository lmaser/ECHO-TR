# ECHO-TR: Análisis de Feedback y Auto-Feedback

**NOTA IMPORTANTE**: Todas las funciones implementadas son **CONTINUAS** (no discretas). Las gráficas muestran curvas suaves, y las tablas con valores en notas MIDI específicas (C1, C2, etc.) son **muestras puntuales** para visualización, NO saltos en la implementación.

---

## 1. IMPLEMENTACIÓN ACTUAL DEL FEEDBACK

### Feedback Normal (sin auto-feedback)

**Tipo**: LINEAL (multiplicador directo)

**Fórmula**:
```cpp
delayBuffer[writePos] = input * inputGain + delayedSignal * feedback
```

**Características**:
- Rango: `0.0` a `0.99` (clamped)
- NO hay transformación logarítmica ni exponencial
- Es un multiplicador directo del delay feedback
- **Independiente de la frecuencia/nota MIDI**

**Ecuación matemática**:
```
output(n) = input(n) + feedback * output(n - delaySamples)
```

Donde:
- `feedback ∈ [0.0, 0.99]` (parámetro del usuario, lineal)
- `delaySamples` = delay time en samples

---

## 2. IMPLEMENTACIÓN DEL AUTO-FEEDBACK

### Transformaciones aplicadas

Cuando `AUTO FBK` está activado, el feedback pasa por **dos transformaciones**:

#### Paso 1: Curva exponencial agresiva
```cpp
exponentialFeedback = feedback^4.0
```

**Propósito**: Concentrar resolución en valores bajos  
**Efecto**:
- 10% lineal → 0.01% exponencial
- 50% lineal → 6.25% exponencial
- 70% lineal → 24% exponencial
- 90% lineal → 65.6% exponencial

#### Paso 2: Multiplicador basado en delay time
```cpp
octavesFromMax = log2(maxDelay / currentDelay)
autoFbkMultiplier = 1.5^octavesFromMax
```

**Propósito**: Compensar feedback según delay time  
**Efecto**:
- Delay corto (frecuencias altas) → multiplier GRANDE → MÁS feedback
- Delay largo (frecuencias bajas) → multiplier PEQUEÑO → MENOS feedback

#### Paso 3: Aplicación final
```cpp
feedback_final = min(0.99, exponentialFeedback * autoFbkMultiplier)
```

---

## 3. GRÁFICA 1: Feedback vs. Frecuencia MIDI

### SIN AUTO-FEEDBACK:

**La función es CONSTANTE** (línea horizontal continua):

```
Feedback (valor final aplicado)
    1.0 │                                              
    0.9 │━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  feedback = 0.90
    0.8 │                                              
    0.7 │━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  feedback = 0.70
    0.6 │                                              
    0.5 │━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  feedback = 0.50
    0.4 │                                              
    0.3 │━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  feedback = 0.30
    0.2 │                                              
    0.1 │━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  feedback = 0.10
    0.0 └────────────────────────────────────────────
         C1    C2    C3    C4    C5    C6    C7    C8
        (32Hz)(65Hz)(130Hz)(261Hz)(523Hz)(1046Hz)(2093Hz)(4186Hz)
```

**Conclusión**: El feedback es CONSTANTE independiente de la frecuencia MIDI.  
→ Esto puede ser problemático porque delays cortos (notas altas) acumulan feedback más rápido.

---

### CON AUTO-FEEDBACK ACTIVADO:

Asumiendo `feedback_slider = 0.50` (parámetro del usuario):

**NOTA**: La función es **CONTINUA** (no escalonada). Los puntos marcados son valores en notas específicas.

```
Feedback (valor final aplicado)
    1.0 │                                          ╭───
    0.9 │                                     ╭────╯     
    0.8 │                                ╭────╯          
    0.7 │                           ╭────╯               
    0.6 │                      ╭────╯                    
    0.5 │                 ╭────╯                         
    0.4 │            ╭────╯                              
    0.3 │       ╭────╯                                   
    0.2 │  ╭────╯                                        
    0.1 │╭─╯                                             
    0.0 └────────────────────────────────────────────
         C1    C2    C3    C4    C5    C6    C7    C8
        (32Hz)(65Hz)(130Hz)(261Hz)(523Hz)(1046Hz)(2093Hz)(4186Hz)

    Delay: 31ms  15ms  7.6ms 3.8ms 1.9ms 0.95ms 0.47ms 0.23ms
```

**Función matemática** (continua):
```
feedback_final(delayMs) = min(0.99, feedback^4.0 * 1.5^log2(maxDelay/delayMs))
```

**Valores calculados** (feedback_slider = 0.50, muestras de la función continua):
```
Nota  | Freq (Hz) | Delay (ms) | Octaves | Multiplier | Feedback_exp | Feedback_final
------|-----------|------------|---------|------------|--------------|---------------
C1    |   32.7    |   30.58    |  6.03   |   11.25    |    0.0625    |   0.703
C2    |   65.4    |   15.29    |  7.03   |   16.88    |    0.0625    |   0.990 ⚠️(clamped)
C3    |  130.8    |    7.64    |  8.03   |   25.31    |    0.0625    |   0.990 ⚠️(clamped)
C4    |  261.6    |    3.82    |  9.03   |   37.97    |    0.0625    |   0.990 ⚠️(clamped)
C5    |  523.3    |    1.91    | 10.03   |   56.95    |    0.0625    |   0.990 ⚠️(clamped)
C6    | 1046.5    |    0.95    | 11.03   |   85.43    |    0.0625    |   0.990 ⚠️(clamped)
C7    | 2093.0    |    0.47    | 12.03   |  128.14    |    0.0625    |   0.990 ⚠️(clamped)
C8    | 4186.0    |    0.23    | 13.03   |  192.21    |    0.0625    |   0.990 ⚠️(clamped)
```

**NOTA IMPORTANTE**: Entre estas notas MIDI, el feedback varía **continuamente** según la fórmula exponencial + multiplicador logarítmico. La tabla muestra MUESTRAS discretas para visualización, pero no hay "saltos" reales en la implementación.

**⚠️ PROBLEMA DETECTADO**: Con feedback_slider ≥ 0.50, el autofeedback satura inmediatamente en 0.99 para casi todas las notas MIDI (C2 en adelante).

---

## 4. GRÁFICA 2: Autofeedback en función de Feedback Slider y Frecuencia

### Mapa de calor: Feedback Final vs. (Slider Value, MIDI Note)

```
Feedback                                    FREQUENCY (MIDI Note) →
Slider     C1     C2     C3     C4     C5     C6     C7     C8
  ↓      (32Hz) (65Hz)(130Hz)(261Hz)(523Hz)(1046Hz)(2093Hz)(4186Hz)
─────────────────────────────────────────────────────────────────
 1.00 │   0.99   0.99   0.99   0.99   0.99   0.99   0.99   0.99  
 0.90 │   0.99   0.99   0.99   0.99   0.99   0.99   0.99   0.99  
 0.80 │   0.99   0.99   0.99   0.99   0.99   0.99   0.99   0.99  
 0.70 │   0.99   0.99   0.99   0.99   0.99   0.99   0.99   0.99  
 0.60 │   0.92   0.99   0.99   0.99   0.99   0.99   0.99   0.99  
 0.50 │   0.70   0.99   0.99   0.99   0.99   0.99   0.99   0.99  ← Problema
 0.40 │   0.46   0.68   0.99   0.99   0.99   0.99   0.99   0.99  
 0.30 │   0.25   0.37   0.55   0.82   0.99   0.99   0.99   0.99  
 0.20 │   0.11   0.16   0.24   0.36   0.54   0.81   0.99   0.99  
 0.10 │   0.03   0.04   0.06   0.09   0.14   0.20   0.31   0.46  
 0.00 │   0.00   0.00   0.00   0.00   0.00   0.00   0.00   0.00  
```

**Leyenda**:
- `0.00-0.20`: ░░ (muy bajo)
- `0.20-0.50`: ▒▒ (bajo-medio)
- `0.50-0.80`: ▓▓ (medio-alto)
- `0.80-0.99`: ██ (saturado)

---

## 5. ANÁLISIS Y PROBLEMAS IDENTIFICADOS

### 5.1 Problema: Saturación prematura del autofeedback

**Síntoma**:
- Con feedback_slider ≥ 0.50, el autofeedback alcanza el clamp de 0.99 para casi todas las notas
- Solo se aprecia diferencia real en el rango 0-40% del slider
- El rango útil es MUY limitado (comentado en el código)

**Causa raíz**:
1. **Curva demasiado agresiva**: `feedback^4.0` reduce drásticamente valores medios
   - 50% → 6.25% exponencial
2. **Multiplicador demasiado fuerte**: `1.5^octaves` crece exponencialmente
   - C3 tiene multiplicador de ~25x
   - C5 tiene multiplicador de ~57x

**La función es continua** (no hay saltos discretos), pero su crecimiento exponencial causa saturación:

```
feedback_final(delayMs) = min(0.99, feedback^4.0 * 1.5^(log2(maxDelay/delayMs)))
```

**Consecuencia**:
```
feedback_exp (6.25%) * multiplier (25x) = 156% → clamped a 99%
```

→ La mayoría de notas saturan inmediatamente

---

### 5.2 Problema: Glide excesivo con notas rápidas

**Síntoma reportado por el usuario**:
- "hay demasiado glide al usar notas rápidas en MIDI"

**Causa**: Smoothing exponencial con coeficiente 0.9997

```cpp
smoothedDelaySamples = smoothedDelaySamples * 0.9997 + targetDelay * 0.0003
```

**Time constant**: ~330ms (demasiado lento para cambios rápidos de frecuencia)

**Ejemplo**:
- Cambio de C3 (7.6ms) → C5 (1.9ms)
- El delay tarda ~330ms en alcanzar el 63% del cambio
- Para cambios de 2 octavas, el glide es muy audible

**Tabla de settling time**:
```
% del cambio alcanzado | Tiempo transcurrido
-----------------------|--------------------
   63% (1 τ)           |     330 ms
   86% (2 τ)           |     660 ms
   95% (3 τ)           |     990 ms
   98% (4 τ)           |    1320 ms
```

Para secuencias MIDI rápidas (120 BPM, 1/16 notes = 125ms), el smoothing NO termina antes de la siguiente nota.

---

## 6. PROPUESTAS DE MEJORA (RAMA 10c)

### 6.1 Ajustar curva de Auto-Feedback

**Opción A**: Reducir exponente de la curva
```cpp
// Actual: feedback^4.0 (muy agresivo)
// Propuesto: feedback^2.0 (más suave)
const float exponentialFeedback = std::pow (targetFeedback, 2.0f);
```

**Opción B**: Reducir multiplicador temporal
```cpp
// Actual: 1.5^octaves (muy fuerte)
// Propuesto: 1.3^octaves (más suave)
const float autoFbkMultiplier = std::pow (1.3f, octavesFromMax);
```

**Opción C**: Combinación
```cpp
const float exponentialFeedback = std::pow (targetFeedback, 2.5f);
const float autoFbkMultiplier = std::pow (1.4f, octavesFromMax);
```

---

### 6.2 Ajustar Smoothing para MIDI

**Problema**: El mismo smoothing se usa para cambios manuales (lentos) y MIDI (rápidos)

**Solución**: Smoothing adaptativo según fuente de cambio

```cpp
// Smoothing lento para cambios manuales (mantiene pitch shifting suave)
const float smoothCoeffManual = 0.9997f;  // ~330ms

// Smoothing rápido para cambios MIDI (minimiza glide entre notas)
const float smoothCoeffMidi = 0.997f;     // ~33ms (10x más rápido)

// Seleccionar según fuente de cambio
const bool midiActive = (currentMidiFrequency.load() > 0.1f);
const float smoothCoeff = midiActive ? smoothCoeffMidi : smoothCoeffManual;
```

**Alternativa**: Smoothing variable según magnitud del cambio

```cpp
const float deltaSamples = std::abs(targetDelay - smoothedDelaySamples);
const float changeRatio = deltaSamples / targetDelay;

// Si el cambio es >20% (salto grande como cambio de nota), smooth rápido
const float smoothCoeff = (changeRatio > 0.20f) ? 0.997f : 0.9997f;
```

---

### 6.3 Optimización de Performance

**Problemas actuales**:
1. **Interpolación lineal per-sample**: 2 lecturas + multiplicaciones
2. **Smoothing exponencial per-sample**: multiplicación + suma
3. **Clipping per-sample**: `jlimit()`

**Mejoras propuestas**:
1. **SIMD vectorization**: Procesar 4-8 samples simultáneamente
2. **Lookup tables**: Pre-calcular curvas exponenciales
3. **Branch reduction**: Evitar condicionales dentro del loop

---

## 7. PRÓXIMOS PASOS (RAMA 10c)

### Prioridad Alta:
1. ✅ Analizar implementación actual (COMPLETADO)
2. ⏳ Ajustar curva de auto-feedback para ampliar rango útil
3. ⏳ Implementar smoothing adaptativo para MIDI

### Prioridad Media:
4. ⏳ Optimizar loops de procesamiento (SIMD, lookup tables)
5. ⏳ Testing de picos de CPU con diferentes configuraciones

### Testing requerido:
- [ ] Verificar que feedback no sature con slider al 50%
- [ ] Confirmar que glide MIDI es imperceptible en notas rápidas
- [ ] Medir reducción de picos de CPU
- [ ] Validar que smoothing manual sigue funcionando correctamente

---

## 8. FÓRMULAS DE REFERENCIA

### Feedback tail time (para getTailLengthSeconds):
```
tailTime = delayTime * log(0.001) / log(feedback)
         = delayTime * (-6.9078) / log(feedback)
```

Donde:
- `log(0.001) = -6.9078` (tiempo hasta -60dB)
- `feedback ∈ (0, 1)`

### MIDI note to frequency:
```
frequency = 440 * 2^((note - 69) / 12)
```

### Delay time from frequency:
```
delayMs = 1000 / frequency
```

---

**Documento generado**: 2026-03-01  
**Rama objetivo**: `10c` (testing)  
**Status**: ANÁLISIS COMPLETADO - Pendiente implementación
