# Materialien (Phase 5)

Code: `libs/materials` · Tests: `tests/test_modes.cpp` · CLI: `photonlab_cli materials [nm]`

## Sellmeier-Gleichung

    n²(λ) = 1 + Σ Bᵢ·λ² / (λ² − Cᵢ²)

Gruppenindex aus der analytischen Ableitung: `n_g = n − λ·dn/dλ` mit
`d(n²)/dλ = Σ Bᵢ·(−2λ·Cᵢ²)/(λ² − Cᵢ²)²`.

| Material | B₁, C₁ [µm] | B₂, C₂ [µm] | B₃, C₃ [µm] | gültig | Quelle |
|---|---|---|---|---|---|
| SiO₂ (Quarzglas) | 0,6961663; 0,0684043 | 0,4079426; 0,1162414 | 0,8974794; 9,896161 | 0,21–3,71 µm | Malitson, J. Opt. Soc. Am. 55, 1205 (1965) |
| Si₃N₄ (LPCVD) | 3,0249; 0,1353406 | 40314; 1239,842 | – | 0,31–5,5 µm | Luke et al., Opt. Lett. 40, 4823 (2015) |
| Si (kristallin) | 10,6684293; 0,301516485 | 0,0030434748; 1,13475115 | 1,54133408; 1104 | 1,36–11 µm | Salzberg/Villa, J. Opt. Soc. Am. 47, 244 (1957); Anpassung Tatian, Appl. Opt. 23, 4477 (1984) |
| Luft | n = 1 | | | | n − 1 < 3·10⁻⁴ |

Werte bei 1550 nm:

| Material | n | n_g | dn/dT [1/K] | Quelle dn/dT |
|---|---|---|---|---|
| SiO₂ | 1,4440 | 1,4626 | 1,0·10⁻⁵ | Leviton/Frey, Proc. SPIE 6273 (2006), ca. |
| Si₃N₄ | 1,9963 | 2,0396 | 2,45·10⁻⁵ | Arbabi/Goddard, Opt. Lett. 38, 3878 (2013) |
| Si | 3,4777 | 3,6053 | 1,8·10⁻⁴ | Bauplan 4.4; vgl. Komma et al., Appl. Phys. Lett. 101, 041905 (2012) |

## Grenzen

- Nur der Realteil der Brechzahl: keine Absorption. Unterhalb von 1,1 µm absorbiert Silizium stark,
  die Gleichung gilt dort nicht; außerhalb des Gültigkeitsbereichs warnt die Oberfläche.
- Raumtemperatur; die Temperatur geht nur linear über `dn/dT` ein.
- Abgeschiedene Schichten (PECVD-Oxid, Nitrid mit anderem Verhältnis) weichen je nach Prozess um
  Prozent ab; die Werte gelten für die Materialien der zitierten Arbeiten.
- Das 2D-FDTD rechnet weiterhin ohne Dispersion: Die Bibliothek liefert dort nur die Brechzahl bei
  der Wellenlänge der Quelle als Vorgabe.
- Abweichung von anderen Tabellen: H. H. Li (1980) gibt für Silizium bei 1550 nm 3,4757 an, die hier
  verwendete Gleichung 3,4777 (0,06 % Unterschied).
