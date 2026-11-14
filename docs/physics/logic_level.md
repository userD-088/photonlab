# Logik-Ebene (Phase 10): Brücke 2 und Event-Simulator

Code: `libs/logic/src/logic_sim.cpp` (`logic_sim.hpp`), Tests `tests/test_logic_sim.cpp`, CLI `photonlab_cli logic-sim`,
Oberfläche: Logik-Labor, Reiter „Logikschaltung“.

## Logikblöcke (Brücke 2)

Ein Logikblock ist ein Gatter mit **gemessenen** Eigenschaften: Wahrheitstabelle, Laufzeit, Schaltenergie,
statische Leistung, Codierung jedes Ein- und Ausgangs, Entscheidungsschwelle der Eingänge, schlechteste Pegel der
Ausgänge (aus der Charakterisierung bei Nenneingängen) und ob er Pegel regeneriert. Drei Familien:

| Familie | Modell | Laufzeit, Energie (Festlegung bzw. Annahme) |
|---|---|---|
| ideal | Wahrheitstabelle, Ausgang 1 mW / 0, Schwelle 0,1 mW | 10 ps, 0 J (Referenz) |
| regeneriert (O/E/O) | wie ideal; steht für Photodiode + Entscheider + Treiber + Modulator mit eigenem Laser | 30 ps, 1 pJ je Pegelwechsel, 2 mW dauernd (Größenordnungen, Annahme) |
| passiv optisch | **echte Schaltungen** des Chip-Designers: Interferenz-UND/ODER (`and_interference`, ODER = gleicher Aufbau mit niedriger Schwelle), Interferenz-XOR (`xor_interference`, phasencodiert), SR-Flip-Flop aus dem Kerr-bistabilen Ring (Phase 8) | Ausgang über den Schaltungslöser; 2 ps (UND/ODER/XOR), 50 ps (Ring) als Annahme |

Bei passiven Blöcken übernimmt der Laser jedes Eingangs Leistung **und Phase** des ankommenden Netzes; das Licht am
Detektor des Ausgangs wird weitergeleitet. Die Phase zwischen Gattern ist als **kontrolliert** angenommen (gleichphasige
Leitungen) – das ist der günstigste Fall; reale Interferenz-Kaskaden brauchen dafür Phasenstabilisierung.

## Simulator

Ereignisgesteuert (vereinfachtes Verilog): Ändert sich ein Netz, werden die angeschlossenen Blöcke ausgewertet; ändert
sich ihr Ausgang (Leistung, Phase oder idealer Wert), wird das neue Signal nach der Laufzeit des Blocks auf das
Ausgangsnetz gelegt. Jedes Netz trägt **zwei** Größen: die physikalische (Leistung, Phase) und den idealen Logikwert,
den eine fehlerfreie Schaltung hätte (ideale Wahrheitstabelle bzw. ideales Flip-Flop). Optischer Fan-out teilt die
Leistung (je Verbraucher 1/N); elektrische Signale werden nicht geteilt. Primäre Eingänge sind äußere Quellen, die
jedem Verbraucher seine Nennleistung liefern (bei Fan-out N also N-mal so viel).

**Bewertung:** Ein primärer Ausgang wird mit der Schwelle seines treibenden Blocks gelesen und mit dem idealen Wert
verglichen. Ist ein Vektor falsch, sucht der Simulator die **erste Ursache**: den Blockeingang mit der kleinsten
Stufe (Abstand von den Eingängen), der anders liest als die ideale Logik, und meldet Netz, Pegel und Schwelle (bei
Phasencodierung die Phase). Liest kein Eingang falsch, liegt der Fehler im Ausgangspegel selbst (Meldung mit Pegel).
Flip-Flops werden nur in Haltephasen bewertet (`check_idle_only`), während Setz-/Rücksetzpulsen ist der Ausgang nicht
definiert. Laufzeit = größte Einschwingzeit über alle Vektoren; Energie je Vektor = Schaltenergie (Pegelwechsel) +
statische Leistung × Periode (1 ns).

**Statische Prüfung** vor der Simulation, je Verbindung (Festlegung):
* Codierung von Treiber und Verbraucher gleich (Intensität, Phase, elektrisch), sonst Fehler; ein primärer Eingang, der
  Blöcke mit verschiedener Codierung treibt, ist ein Fehler.
* Pegel: schlechteste 1 des Treibers (nach Fan-out-Aufteilung) über der Schwelle des Verbrauchers, schlechteste 0
  darunter, sonst Fehler; weniger als 20 % Abstand ist eine Warnung.
* Treiber ohne Regeneration: Warnung (seine Pegel hängen von seinen Eingängen ab; Fehler bauen sich in der Kaskade auf).

## Ergebnisse (Abnahme: „4-Bit-Addierer rechnet korrekt oder meldet Kaskadierungsproblem nachvollziehbar“)

| Design | ideal | regeneriert | passiv optisch |
|---|---|---|---|
| Halbaddierer (2 Blöcke) | 4/4 | 4/4, 30 ps | 2/4 – Codierung: XOR braucht Phase, UND Intensität; die Eingänge können nicht beide bedienen |
| 4-Bit-Addierer (20 Blöcke, Übertragskette) | 512/512, 80 ps | **512/512, 240 ps, 47,7 pJ je Vektor** | 19/512 – 36 Fehler in der Prüfung (Codierung, Pegel), jede falsche Zeile mit Ursache |
| UND-Kette, 4 Stufen | 32/32 | 32/32 | 20/32 – die „0“ eines Interferenz-UND ist nicht dunkel (P/2); das nächste UND liest z. B. „und4.A liest 1 statt 0: Pegel 1,46 mW, Schwelle 0,5 mW“ |
| SR-Flip-Flop | 6/6 | 6/6 | 6/6 – Kerr-Ring: Klappen bei 34,0 / 19,2 mW, Halten mit 26,6 mW, Setzpuls 11,1 mW (ohne TPA und Erwärmung) |

Ein einzelnes passives UND arbeitet (4/4). Die Kaskade scheitert an fehlender Regeneration und an der Codierung; mit
Regeneration rechnet der Addierer, kostet aber pro Gatter 30 ps, 1 pJ je Wechsel und 2 mW Dauerleistung – genau die
Abwägung, die der Bauplan (7.8, Kriterien nach Miller) beschreibt. Der Kerr-Speicher hält ohne Pulse, braucht aber
dauernd über 20 mW Haltelicht.

## Grenzen

* Laufzeiten fest je Block (keine Abhängigkeit vom Pegel, keine Anstiegszeiten); Einschwingen von Resonatoren nur als
  feste Verzögerung (die Dynamik steht im Zeitbereich, Phase 9).
* Phase zwischen Gattern als kontrolliert angenommen; Wellenlängen (WDM) nicht unterschieden.
* Rauschen auf der Logik-Ebene fehlt (Pegelreserve nur über die statische Prüfung).
* Kein Schieberegister/Takt bisher (Bauplan 8.2); Speicherzelle mit PCM gibt es als Bauteil (Phase 9), noch nicht als Logikblock.
