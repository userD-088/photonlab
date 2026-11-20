#include "help.hpp"

#include "imgui.h"

// Hinweis: Alle Texte bleiben im Latin-1-Zeichenvorrat (eingebaute ImGui-Schrift): keine Pfeile, kein
// Griechisch.

namespace photonlab::gui {
namespace {

void heading(const char* text) {
  ImGui::Spacing();
  ImGui::SeparatorText(text);
}

void paragraph(const char* text) { ImGui::TextWrapped("%s", text); }

void bullet(const char* text) {
  ImGui::Bullet();
  ImGui::TextWrapped("%s", text);
}

// Zweispaltige Tabelle (Taste oder Begriff, Erklärung)
void table(const char* id, const char* const (*rows)[2], int count) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
  ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 0.35f);
  ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 0.65f);
  for (int k = 0; k < count; ++k) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(rows[k][0]);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", rows[k][1]);
  }
  ImGui::EndTable();
}

void overview() {
  paragraph(
      "PhotonLab simuliert integrierte Photonik: Licht in Wellenleitern auf einem Chip. Es gibt zwei "
      "Arbeitsbereiche, die aufeinander aufbauen.");
  heading("Bauteil-Labor (Feld)");
  paragraph(
      "Hier entsteht ein einzelnes Bauteil - ein Koppler, ein Ring, ein Gitter, ein Kristall - aus Formen "
      "mit "
      "Brechzahlen. PhotonLab rechnet das elektromagnetische Feld Zeitschritt für Zeitschritt (FDTD) und "
      "zeigt "
      "es live: schnell in 2D (Draufsicht), genauer in 3D (die Szene als echte Schicht auf einem Chip).");
  heading("Chip-Designer (Schaltung)");
  paragraph(
      "Hier werden Bauteile zu einer Schaltung verbunden: Laser, Wellenleiter, Koppler, Ringe, Heizer, "
      "MZI-Schalter, Photodioden. Jedes Bauteil ist eine S-Matrix (wie viel Licht mit welcher Phase von "
      "jedem "
      "Anschluss zu jedem anderen läuft); so rechnet auch eine Schaltung mit hundert Bauteilen in "
      "Millisekunden. Das Fenster Logik schaltet Bits und prüft, ob die Schaltung rechnet - bis zum "
      "8-Bit-Addierer.");
  heading("Die Brücke");
  paragraph(
      "Ein im Bauteil-Labor gezeichnetes Bauteil wird im Reiter 'Bauteil für den Chip-Designer' einmal "
      "vermessen; die gemessene S-Matrix steht danach im Chip-Designer als Bauteil 'sparam:<name>' zur "
      "Verfügung, gespeichert im Projekt.");
  heading("Was PhotonLab ehrlich sagt");
  paragraph(
      "Jede Zahl stammt aus einer Rechnung, nicht aus einer Annahme. Wo ein Modell vereinfacht (2D statt 3D, "
      "stationär statt zeitabhängig, ideale Koppler), steht es im Programm als Hinweis und in docs/physics. "
      "Gatter bekommen ein Zeugnis mit Ampeln statt eines bloßen 'funktioniert'.");
  heading("Dateien");
  paragraph(
      "Ein Projekt (.plab.json) enthält Schaltung, Feld-Szene, Querschnitt, Logik-Definition und die "
      "Bauteilbibliothek - alle Zahlen in SI-Einheiten. Datei > Beispiele lädt fertige Projekte beider "
      "Bereiche.");
}

void first_steps() {
  heading("1  Licht im Wellenleiter sehen");
  bullet("Datei > Beispiele: Feld > Grundlagen > Gerader Wellenleiter.");
  bullet("Das Feld läuft sofort los; Leertaste hält an, R setzt zurück.");
  bullet("Unten zeigen die Monitore Feld, Leistungsfluss und Spektrum.");
  bullet("In der Werkzeugleiste '3D' wählen und '3D rechnen': dieselbe Szene als 220-nm-Siliziumschicht.");
  heading("2  Einen Addierer rechnen lassen");
  bullet("Datei > Beispiele: Schaltung > 8-Bit-Addierer.");
  bullet("Im Fenster Logik bei A und B Zahlen eintippen (0 bis 255).");
  bullet("Auf der Zeichenfläche folgt das Licht den Schaltern; unten steht das Ergebnis mit Probe.");
  bullet(
      "Kleinere Addierer (Halb-, Voll-, 2-Bit) zeigen zusätzlich die ganze Wahrheitstabelle und ein "
      "Zeugnis.");
  heading("3  Eigenes Bauteil in den Chip");
  bullet("Im Bauteil-Labor Formen zeichnen (Umschalt+R für ein Rechteck), Wellenleiter bis an den Rand.");
  bullet("Werkzeugleiste 'Als Bauteil ...': 'Ports automatisch setzen', dann 'S-Matrix messen'.");
  bullet("'... und in die Schaltung setzen': das Bauteil liegt im Chip-Designer und lässt sich verbinden.");
  heading("4  Eine Schaltung bauen");
  bullet("Datei > Neu, dann in der Bauteilbibliothek links Bauteile anklicken.");
  bullet("Ports verbinden: von einem Anschluss zum anderen ziehen.");
  bullet(
      "Bauteil anklicken: Parameter im Fenster Eigenschaften; Kennlinie zeigt die Wirkung eines Parameters.");
}

void field_lab() {
  paragraph("Reiter des Bauteil-Labors:");
  static const char* const tabs[][2] = {
      {"Feld",
       "Szene bauen und rechnen. Links Rechnung, neue Objekte, Objektliste und Eigenschaften; "
       "rechts das Feld und die Monitore. Werkzeugleiste: Titel, 2D/3D, Start, Rückgängig, Darstellung."},
      {"Bauteil für den Chip-Designer",
       "Ports festlegen (auch automatisch), S-Matrix messen, als Bauteil in die "
       "Bibliothek und auf Wunsch gleich in die Schaltung."},
      {"Logik prüfen",
       "Wahrheitstabellen von Gattern auf Feld-Ebene (Halb- und Volladdierer im Feld, Kristall-"
       "Gatter) aus vollen FDTD-Rechnungen, mit Zeugnis."},
      {"Moden und Bänder",
       "Modenlöser für Wellenleiter-Querschnitte (n_eff, Gruppenindex) und Bandstruktur "
       "photonischer Kristalle (Bandlücken); erzeugt Kristall-Szenen."},
      {"Inverses Design",
       "Der Rechner formt ein Bauteil selbst (Adjoint-Methode), z. B. einen kompakten Knick."}};
  table("reiter", tabs, 5);
  heading("Objekte der Szene");
  static const char* const objects[][2] = {
      {"Form", "Rechteck, Kreis, Ring, Taper, Bogen mit einer Brechzahl. Spätere Formen liegen oben."},
      {"Quelle",
       "Punkt, Linie (gleichmäßig oder Gauß) oder Mode (Profil der geführten Mode unter der Linie). "
       "Dauerstrich oder Puls; Modenquellen können nur in eine Richtung strahlen. Mit einem "
       "Auslöser wird die Quelle zum Regenerator (Detektor, Elektronik, Sender)."},
      {"Monitor",
       "Punkt (Feld über der Zeit), Fluss (Leistung durch eine Linie) oder Mode (Amplitude der "
       "geführten Mode, ohne Streulicht)."},
      {"Port",
       "Anschluss für die S-Matrix: Linie quer über einen Wellenleiter am Rand. Ändert die Rechnung nicht."},
      {"Gruppe",
       "Lage und Drehung für mehrere Objekte; Bausteine (Koppler, Halbaddierer-Chip ...) sind Gruppen."}};
  table("objekte", objects, 5);
  heading("2D oder 3D");
  paragraph(
      "2D rechnet die Draufsicht: schnell (auch auf der Grafikkarte), live, aber ohne Abstrahlung nach oben "
      "und "
      "unten. Die Brechzahlen der Formen sind dort effektive Indizes (2,85 steht für 220 nm Silizium). 3D "
      "macht "
      "aus der Szene eine echte Schicht (Dicke, Kernmaterial, Mantel einstellbar) und rechnet alle sechs "
      "Feldkomponenten; das dauert Minuten. Flüsse in 3D vergleicht man am besten relativ zueinander.");
  heading("Die 3D-Umgebung");
  paragraph(
      "Im 3D-Modus steht die Szene als Chip im Raum: Formen als Körper in der Schicht, darunter das "
      "Substrat, "
      "darüber der Mantel (Umriss), Quellen, Monitore und Ports als Flächen quer durch den Aufbau - so "
      "speisen "
      "und messen sie in der 3D-Rechnung. Nach '3D rechnen' liegt das Feld als zwei Schnittebenen im Raum "
      "(waagerecht in der Mitte der Schicht, senkrecht entlang x); schwaches Feld ist durchsichtig.");
  static const char* const mouse[][2] = {
      {"Linke Maustaste",
       "Objekt wählen (Strg/Umschalt: dazu) und auf der Schicht verschieben; ins Leere: drehen"},
      {"Rechte Maustaste", "ziehen: drehen; klicken: Menü des Objekts"},
      {"Mittlere Maustaste, Mausrad", "verschieben, zoomen"},
      {"Schräg, Oben, Vorne, Seite", "feste Blickrichtungen; Einpassen (auch F) zeigt die ganze Szene"},
      {"Höhe x3", "Überhöhung in z, nur für die Anzeige (die Schicht ist sonst hauchdünn)"}};
  table("maus3d", mouse, 5);
}

void chip_designer() {
  static const char* const windows[][2] = {
      {"Zeichenfläche",
       "Bauteile und Verbindungen. Farbton der Verbindung = Phase, Helligkeit = Leistung. "
       "'Werte' blendet Leistung und Phase an jeder Verbindung ein."},
      {"Bauteilbibliothek", "Klick fügt ein Bauteil ein. Unten die selbst gemessenen Bauteile (sparam:...)."},
      {"Eigenschaften", "Parameter des gewählten Bauteils und alle Messwerte der Schaltung."},
      {"Spektrum",
       "Leistungen über der Wellenlänge mit Resonanz-Tabelle (FSR, Güte); Klick legt die "
       "Arbeitswellenlänge auf eine Resonanz."},
      {"Kennlinie", "Detektorleistungen über einem Parameter des gewählten Bauteils (z. B. Heizleistung)."},
      {"Monte-Carlo", "Fertigungsstreuung: Parameter normalverteilt stören, Histogramm und Ausbeute."},
      {"Logik",
       "Eingänge (Heizer, Laser ...) und Ausgänge (Detektoren) eines Gatters; Bits und Zahlen von Hand "
       "schalten, Wahrheitstabelle und Zeugnis bis 6 Eingänge."}};
  table("fenster", windows, 7);
  heading("Unterschaltungen (Hierarchie)");
  paragraph(
      "Eine fertige Schaltung - etwa eine Volladdierer-Zelle - wird mit 'Schaltung als Unterschaltung ...' "
      "(Bauteilbibliothek, unten) zu einem Bauteil: offene Ports führen nach außen, die Eingänge der Logik "
      "werden seine Parameter. Danach lässt sie sich beliebig oft einsetzen; 'Bearbeiten' öffnet ihr "
      "Inneres, "
      "'Zurück zur Schaltung' übernimmt die Änderungen für alle Instanzen. Beispiel: 8-Bit-Addierer aus "
      "Zellen.");
  heading("Bedienen");
  bullet("Verbinden: von Port zu Port ziehen; Trennen: Rechtsklick auf einen Port.");
  bullet("Verschieben: Bauteil ziehen; Löschen: Entf oder Rechtsklick.");
  bullet("Ansicht: Fläche ziehen verschiebt, Mausrad zoomt, 'Alles zeigen' passt ein.");
  heading("Vom Schaltplan zum Chip");
  paragraph(
      "Datei > 'Schaltung als Chip-Layout (GDSII) exportieren' zeichnet die Schaltung als Maskenentwurf: "
      "jedes "
      "Bauteil als Zelle (Koppler mit berechneter Länge, MZI-Schalter mit Heizern, Ringe, Gitterkoppler, "
      "Photodioden), platziert wie im Editor (30 µm je Rasterzelle), Verbindungen als Wellenleiter mit "
      "Bögen. "
      "Kreuzungen und fehlende Teile (Bondflächen, Abstandsregeln) meldet die Statuszeile.");
  heading("Baubar");
  paragraph(
      "Die Addierer-Beispiele bestehen nur aus Teilen eines Standard-Siliziumprozesses: Gitterkoppler, "
      "Richtkoppler, MZI-Schalter mit Heizern, Germanium-Photodioden. Die Summanden sind Heizströme, der "
      "Übertrag läuft als Licht. Grenzen: Heizer schalten in Mikrosekunden, und jedes Bit kostet einige zehn "
      "Milliwatt Heizleistung (unterätzte Heizer deutlich weniger).");
}

void shortcuts() {
  heading("Überall");
  static const char* const global[][2] = {{"Strg+N / Strg+O", "neues Projekt / öffnen"},
                                          {"Strg+S / Strg+Umschalt+S", "speichern / speichern unter"},
                                          {"F11", "Vollbild"}};
  table("global", global, 3);
  heading("Bauteil-Labor, Reiter Feld");
  static const char* const field[][2] = {
      {"Leertaste, R", "Start / Pause, Zurücksetzen"},
      {"Umschalt+R/K/I/T/B", "neues Rechteck, Kreis, Ring, Taper, Bogen"},
      {"Umschalt+G/Q/M/P", "neue Gruppe, Quelle, Monitor, Port (in der gewählten Gruppe)"},
      {"Entf, Rücktaste", "Löschen"},
      {"Strg+Z, Strg+Y", "Rückgängig, Wiederholen"},
      {"Strg+C / X / V / D", "Kopieren, Ausschneiden, Einfügen, Duplizieren"},
      {"Strg+A, Esc", "alles auswählen, Auswahl aufheben"},
      {"Strg+G, Strg+Umschalt+G", "gruppieren, aus Gruppe lösen"},
      {"Pfeiltasten", "verschieben um eine Zelle (Umschalt: 10 Zellen)"},
      {"Q / E", "drehen um -15 / +15 Grad (Alt: 1 Grad)"},
      {"Bild auf / Bild ab", "Form ganz nach vorn / hinten"},
      {"H", "aktivieren / deaktivieren"},
      {"F, + / -", "Ansicht einpassen, zoomen (auch Mausrad; mittlere Maustaste verschiebt)"}};
  table("feld", field, 13);
  heading("Chip-Designer");
  static const char* const chip[][2] = {{"Entf", "gewähltes Bauteil löschen"},
                                        {"Mausrad, Ziehen", "zoomen, Fläche verschieben"}};
  table("chip", chip, 2);
}

void physics() {
  heading("Feld");
  bullet(
      "FDTD nach Yee mit absorbierender Randschicht (CPML); 2D in zwei Polarisationen (Ez, Hz), 3D "
      "vollvektoriell.");
  bullet(
      "Brechzahlen ohne Dispersion; Kanten in Treppenstufen der Zellgröße; nichtlineare Formen (Kerr, TPA) "
      "nur in 2D.");
  bullet("2D-Flüsse gelten je Meter Bauteilhöhe (W/m); in 3D sind es Watt bei einer Quelle mit 1 V/m.");
  heading("Schaltung");
  bullet("Stationär und monochromatisch: S-Matrizen bei einer Wellenlänge, ein lineares Gleichungssystem.");
  bullet("Reflexionsfrei, außer ein Bauteil (z. B. aus einer Messung) bringt Reflexion mit.");
  bullet("Nichtlineare Bauteile (Kerr-Ring, SOA) werden selbstkonsistent iteriert; Hysterese über Rampen.");
  bullet("Keine Zeitdynamik der Heizer und Modulatoren; Rauschen nur in der Photodiode.");
  heading("Logik");
  bullet("Bit = Leistung über einer Schwelle. Kontrast = 10 log10(schlechteste 1 / schlechteste 0).");
  bullet(
      "Zeugnis nach den Kriterien von D. A. B. Miller (2010); die Schwellen der Ampeln sind Festlegungen.");
  heading("Nachlesen");
  paragraph("Herleitungen, Quellen der Materialdaten und Prüfungen stehen in docs/physics im Projektordner.");
}

void command_line() {
  paragraph("photonlab_cli rechnet Projekte ohne Oberfläche, z. B. für Serien und Tests:");
  static const char* const rows[][2] = {
      {"run <datei>", "Schaltung bei der Arbeitswellenlänge"},
      {"run <datei> --sweep / --logic", "Wellenlängen-Sweep / Wahrheitstabelle und Zeugnis"},
      {"run <datei> --field / --field3d", "Feld-Szene über ihre Laufzeit in 2D / als Schicht in 3D"},
      {"run <datei> --sparam", "S-Matrix der Feld-Szene an ihren Ports"},
      {"example <name>", "Beispiel als .plab.json ausgeben (z. B. adder8)"},
      {"bench [--gpu]", "Rechengeschwindigkeit auf diesem Rechner"},
      {"validate", "Prüfung gegen analytische Lösungen"}};
  table("cli", rows, 7);
  paragraph("Ohne Argumente zeigt photonlab_cli alle Befehle.");
}

}  // namespace

void draw_help_window(bool* open, HelpTopic* topic) {
  ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 52.0f, ImGui::GetFontSize() * 34.0f),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Hilfe", open)) {
    ImGui::End();
    return;
  }
  struct Entry {
    HelpTopic topic;
    const char* label;
  };
  static const Entry entries[] = {
      {HelpTopic::overview, "Überblick"},        {HelpTopic::first_steps, "Erste Schritte"},
      {HelpTopic::field_lab, "Bauteil-Labor"},   {HelpTopic::chip_designer, "Chip-Designer"},
      {HelpTopic::shortcuts, "Tastenkürzel"},    {HelpTopic::physics, "Physik und Grenzen"},
      {HelpTopic::command_line, "Kommandozeile"}};
  ImGui::BeginChild("themen", ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f), ImGuiChildFlags_Borders);
  for (const auto& e : entries)
    if (ImGui::Selectable(e.label, *topic == e.topic)) *topic = e.topic;
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("inhalt", ImVec2(0.0f, 0.0f));
  switch (*topic) {
    case HelpTopic::overview:
      overview();
      break;
    case HelpTopic::first_steps:
      first_steps();
      break;
    case HelpTopic::field_lab:
      field_lab();
      break;
    case HelpTopic::chip_designer:
      chip_designer();
      break;
    case HelpTopic::shortcuts:
      shortcuts();
      break;
    case HelpTopic::physics:
      physics();
      break;
    case HelpTopic::command_line:
      command_line();
      break;
  }
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace photonlab::gui
