// Plain-English names for airlines (ICAO callsign prefix) and aircraft
// types (ICAO type designator), so labels read "Delta 1234 / Boeing 737"
// instead of "DAL1234 / B738".
#pragma once
#include <string.h>
#include <ctype.h>
#include <stdio.h>

struct NamePair { const char *code; const char *name; };

static const NamePair AIRLINES[] = {
  // United States
  {"AAL", "American"}, {"DAL", "Delta"}, {"UAL", "United"}, {"SWA", "Southwest"},
  {"JBU", "JetBlue"}, {"ASA", "Alaska"}, {"NKS", "Spirit"}, {"FFT", "Frontier"},
  {"AAY", "Allegiant"}, {"SCX", "Sun Country"}, {"HAL", "Hawaiian"}, {"MXY", "Breeze"},
  {"VXP", "Avelo"}, {"SKW", "SkyWest"}, {"RPA", "Republic"}, {"ENY", "Envoy"},
  {"EDV", "Endeavor"}, {"JIA", "PSA"}, {"ASH", "Mesa"}, {"GJS", "GoJet"},
  {"QXE", "Horizon"}, {"PDT", "Piedmont"}, {"CPZ", "Compass"}, {"AWI", "Air Wisconsin"},
  {"UCA", "CommuteAir"}, {"SIL", "Silver"}, {"CPA", "Cathay"},
  {"FDX", "FedEx"}, {"UPS", "UPS"}, {"GTI", "Atlas Air"}, {"ABX", "ABX Air"},
  {"ATN", "Air Transport"}, {"PAC", "Polar"}, {"KAL", "Korean Air"},
  {"EJA", "NetJets"}, {"LXJ", "Flexjet"}, {"XOJ", "XOJet"}, {"JRE", "JetReady"},
  {"VJA", "VistaJet"}, {"WUP", "Wheels Up"},
  {"RCH", "US Air Force"}, {"CNV", "US Navy"}, {"PAT", "US Army"}, {"CGX", "Coast Guard"},
  // Canada / Mexico / Latin America
  {"ACA", "Air Canada"}, {"JZA", "Air Canada Exp"}, {"WJA", "WestJet"}, {"WEN", "WestJet Encore"},
  {"POE", "Porter"}, {"TSC", "Air Transat"}, {"FLE", "Flair"}, {"SWG", "Sunwing"},
  {"AMX", "Aeromexico"}, {"VOI", "Volaris"}, {"VIV", "Viva Aerobus"}, {"CMP", "Copa"},
  {"AVA", "Avianca"}, {"LAN", "LATAM"}, {"TAM", "LATAM"}, {"GLO", "Gol"}, {"AZU", "Azul"},
  {"BWA", "Caribbean"}, {"ARG", "Aerolineas"},
  // Europe
  {"BAW", "British Airways"}, {"VIR", "Virgin Atlantic"}, {"EZY", "easyJet"}, {"RYR", "Ryanair"},
  {"DLH", "Lufthansa"}, {"AFR", "Air France"}, {"KLM", "KLM"}, {"IBE", "Iberia"},
  {"EIN", "Aer Lingus"}, {"SWR", "Swiss"}, {"AUA", "Austrian"}, {"SAS", "SAS"},
  {"FIN", "Finnair"}, {"ICE", "Icelandair"}, {"TAP", "TAP Portugal"}, {"THY", "Turkish"},
  {"ITY", "ITA Airways"}, {"LOT", "LOT Polish"}, {"BEL", "Brussels"}, {"NAX", "Norwegian"},
  {"WZZ", "Wizz Air"}, {"VLG", "Vueling"}, {"EWG", "Eurowings"}, {"CFG", "Condor"},
  {"DHK", "DHL"}, {"BCS", "DHL"}, {"TOM", "TUI"},
  // Middle East / Asia / Oceania
  {"UAE", "Emirates"}, {"QTR", "Qatar"}, {"ETD", "Etihad"}, {"SVA", "Saudia"},
  {"ELY", "El Al"}, {"JAL", "Japan Airlines"}, {"ANA", "ANA"}, {"AAR", "Asiana"},
  {"CCA", "Air China"}, {"CES", "China Eastern"}, {"CSN", "China Southern"},
  {"EVA", "EVA Air"}, {"CAL", "China Airlines"}, {"SIA", "Singapore"},
  {"AIC", "Air India"}, {"THA", "Thai"}, {"PAL", "Philippine"}, {"QFA", "Qantas"},
  {"ANZ", "Air New Zealand"}, {"VOZ", "Virgin Australia"},
};

static const NamePair TYPES[] = {
  // Boeing
  {"B712", "Boeing 717"}, {"B733", "Boeing 737"}, {"B734", "Boeing 737"}, {"B735", "Boeing 737"},
  {"B736", "Boeing 737"}, {"B737", "Boeing 737"}, {"B738", "Boeing 737"}, {"B739", "Boeing 737"},
  {"B37M", "Boeing 737 MAX"}, {"B38M", "Boeing 737 MAX"}, {"B39M", "Boeing 737 MAX"}, {"B3XM", "Boeing 737 MAX"},
  {"B744", "Boeing 747"}, {"B748", "Boeing 747"}, {"B74F", "Boeing 747"},
  {"B752", "Boeing 757"}, {"B753", "Boeing 757"},
  {"B762", "Boeing 767"}, {"B763", "Boeing 767"}, {"B764", "Boeing 767"},
  {"B772", "Boeing 777"}, {"B77L", "Boeing 777"}, {"B773", "Boeing 777"}, {"B77W", "Boeing 777"},
  {"B778", "Boeing 777X"}, {"B779", "Boeing 777X"},
  {"B788", "Boeing 787"}, {"B789", "Boeing 787"}, {"B78X", "Boeing 787"},
  // Airbus
  {"BCS1", "Airbus A220"}, {"BCS3", "Airbus A220"},
  {"A318", "Airbus A318"}, {"A319", "Airbus A319"}, {"A320", "Airbus A320"}, {"A321", "Airbus A321"},
  {"A19N", "Airbus A319neo"}, {"A20N", "Airbus A320neo"}, {"A21N", "Airbus A321neo"},
  {"A332", "Airbus A330"}, {"A333", "Airbus A330"}, {"A338", "Airbus A330neo"}, {"A339", "Airbus A330neo"},
  {"A343", "Airbus A340"}, {"A346", "Airbus A340"}, {"A359", "Airbus A350"}, {"A35K", "Airbus A350"},
  {"A388", "Airbus A380"}, {"A306", "Airbus A300"}, {"A310", "Airbus A310"},
  // Regional
  {"E135", "Embraer 135"}, {"E145", "Embraer 145"}, {"E170", "Embraer 170"},
  {"E75S", "Embraer 175"}, {"E75L", "Embraer 175"}, {"E190", "Embraer 190"},
  {"E195", "Embraer 195"}, {"E290", "Embraer E2"}, {"E295", "Embraer E2"},
  {"CRJ2", "Bombardier CRJ"}, {"CRJ7", "Bombardier CRJ"}, {"CRJ9", "Bombardier CRJ"}, {"CRJX", "Bombardier CRJ"},
  {"DH8A", "Dash 8"}, {"DH8B", "Dash 8"}, {"DH8C", "Dash 8"}, {"DH8D", "Dash 8 Q400"},
  {"AT43", "ATR 42"}, {"AT45", "ATR 42"}, {"AT72", "ATR 72"}, {"AT76", "ATR 72"},
  {"SF34", "Saab 340"}, {"B190", "Beech 1900"}, {"MD11", "MD-11"}, {"MD88", "MD-88"}, {"DC10", "DC-10"},
  // Business jets
  {"C510", "Cessna Mustang"}, {"C525", "Cessna CitationJet"}, {"C25A", "Cessna Citation"},
  {"C25B", "Cessna Citation"}, {"C25C", "Cessna Citation"}, {"C550", "Cessna Citation"},
  {"C560", "Cessna Citation"}, {"C56X", "Citation Excel"}, {"C680", "Citation Sovereign"},
  {"C68A", "Citation Latitude"}, {"C700", "Citation Longitude"}, {"C750", "Citation X"},
  {"E50P", "Embraer Phenom"}, {"E55P", "Embraer Phenom"}, {"E545", "Embraer Legacy"},
  {"CL30", "Challenger 300"}, {"CL35", "Challenger 350"}, {"CL60", "Challenger 600"},
  {"GLEX", "Global Express"}, {"GL5T", "Global 5000"}, {"GL7T", "Global 7500"},
  {"GLF4", "Gulfstream IV"}, {"GLF5", "Gulfstream V"}, {"GLF6", "Gulfstream G650"},
  {"G280", "Gulfstream G280"}, {"LJ35", "Learjet 35"}, {"LJ45", "Learjet 45"}, {"LJ60", "Learjet 60"},
  {"FA50", "Falcon 50"}, {"FA7X", "Falcon 7X"}, {"F2TH", "Falcon 2000"}, {"F900", "Falcon 900"},
  {"PC24", "Pilatus PC-24"}, {"HDJT", "HondaJet"}, {"SF50", "Cirrus Jet"},
  // Light aircraft
  {"C150", "Cessna 150"}, {"C152", "Cessna 152"}, {"C172", "Cessna 172"}, {"C182", "Cessna 182"},
  {"C206", "Cessna 206"}, {"C208", "Cessna Caravan"}, {"C210", "Cessna 210"}, {"C310", "Cessna 310"},
  {"P28A", "Piper Cherokee"}, {"P28R", "Piper Arrow"}, {"P28B", "Piper Cherokee"}, {"P32R", "Piper Saratoga"},
  {"PA34", "Piper Seneca"}, {"PA44", "Piper Seminole"}, {"PA46", "Piper Malibu"},
  {"SR20", "Cirrus SR20"}, {"SR22", "Cirrus SR22"}, {"S22T", "Cirrus SR22"},
  {"BE33", "Beech Bonanza"}, {"BE35", "Beech Bonanza"}, {"BE36", "Beech Bonanza"},
  {"BE58", "Beech Baron"}, {"BE9L", "King Air"}, {"BE20", "King Air"}, {"B350", "King Air"},
  {"M20P", "Mooney"}, {"M20T", "Mooney"}, {"DA40", "Diamond DA40"}, {"DA42", "Diamond DA42"},
  {"DA62", "Diamond DA62"}, {"PC12", "Pilatus PC-12"}, {"TBM7", "TBM 700"}, {"TBM8", "TBM 850"},
  {"TBM9", "TBM 900"}, {"KODI", "Kodiak"}, {"AA5", "Grumman Tiger"}, {"RV7", "Van's RV-7"},
  {"RV8", "Van's RV-8"}, {"RV10", "Van's RV-10"}, {"C77R", "Cessna Cardinal"},
  // Helicopters
  {"R22", "Robinson R22"}, {"R44", "Robinson R44"}, {"R66", "Robinson R66"},
  {"B06", "Bell JetRanger"}, {"B407", "Bell 407"}, {"B429", "Bell 429"}, {"B412", "Bell 412"},
  {"B505", "Bell 505"}, {"EC20", "Airbus H120"}, {"EC30", "Airbus H130"}, {"EC35", "Airbus H135"},
  {"EC45", "Airbus H145"}, {"EC55", "Airbus H155"}, {"EC75", "Airbus H175"},
  {"AS50", "Airbus AStar"}, {"AS55", "Airbus TwinStar"}, {"AS65", "Airbus Dauphin"},
  {"A109", "Leonardo AW109"}, {"A119", "Leonardo AW119"}, {"A139", "Leonardo AW139"},
  {"A169", "Leonardo AW169"}, {"S76", "Sikorsky S-76"}, {"S92", "Sikorsky S-92"},
  {"H60", "Black Hawk"}, {"H47", "Chinook"}, {"H64", "Apache"}, {"MD52", "MD 520"}, {"MD60", "MD 600"},
  {"V22", "Osprey"},
  // Military
  {"C130", "C-130 Hercules"}, {"C30J", "C-130J Hercules"}, {"C17", "C-17 Globemaster"},
  {"C5M", "C-5 Galaxy"}, {"K35R", "KC-135 tanker"}, {"KC46", "KC-46 tanker"},
  {"E3TF", "AWACS"}, {"P8", "P-8 Poseidon"}, {"F15", "F-15 fighter"}, {"F16", "F-16 fighter"},
  {"F18S", "F/A-18 fighter"}, {"F18H", "F/A-18 fighter"}, {"F35", "F-35 fighter"}, {"F22", "F-22 fighter"},
  {"A10", "A-10 Warthog"}, {"B52", "B-52 bomber"}, {"B1", "B-1 bomber"}, {"T38", "T-38 trainer"},
  {"TEX2", "T-6 trainer"}, {"C12", "C-12 Huron"}, {"C40", "C-40 Clipper"},
  // Other
  {"GLID", "Glider"}, {"BALL", "Balloon"},
};

static inline const char *lookupName(const NamePair *tbl, size_t n, const char *code) {
  for (size_t i = 0; i < n; i++)
    if (!strcmp(tbl[i].code, code)) return tbl[i].name;
  return nullptr;
}

// "Boeing 737" for "B738". Unknown types fall back to the code itself.
static inline const char *aircraftModel(const char *type) {
  if (!type || !type[0]) return nullptr;
  const char *n = lookupName(TYPES, sizeof TYPES / sizeof TYPES[0], type);
  return n ? n : type;
}

// "Delta 1234" for "DAL1234". Registrations (N123AB) and unknown codes are
// returned unchanged.
static inline void friendlyFlight(const char *call, char *out, size_t n) {
  if (!call || !call[0]) { out[0] = 0; return; }
  size_t len = strlen(call);
  if (len >= 4 && isalpha(call[0]) && isalpha(call[1]) && isalpha(call[2]) && isdigit(call[3])) {
    char pre[4] = {call[0], call[1], call[2], 0};
    const char *a = lookupName(AIRLINES, sizeof AIRLINES / sizeof AIRLINES[0], pre);
    if (a) {
      const char *num = call + 3;
      while (*num == '0' && num[1]) num++;   // "UAL0012" -> "United 12"
      snprintf(out, n, "%s %s", a, num);
      return;
    }
  }
  snprintf(out, n, "%s", call);
}
