// #define DBG
#define DBG_PULSES

#include <SPI.h>
#include <Ethernet.h>

#define AVEC_DEBITMETRE                 /* Controle avec le debitmetre */

#define COEFFICIENT_DEBITMETRE  7.50    /* Pulse frequency (Hz) / 7.5 = flow rate in L/min */
#define T_OUVERTURE_VANNE       5
#define T_MONTEE_EN_PRESSION    20
#define T_DESCENTE_PRESSION     10

#define NBPULSES_MIN            8    /* Debit(L/mn) x COEFFICIENT_DEBITMETRE  soit 1 L/mn seuil min de débit */

#define PinDebitmetre         3       /* Entrée digital du débitmetre */
#define PinPompe              4       /* Sortie digital pour commander les relais de la pompe  */
#define PinVanne              5       /* Sortie digital pour commander l'electro vanne */
#define PinDebug              6       /* Sortie digital pour commander l'electro vanne */

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; //physical mac address
#define PORT 4200
IPAddress ip(192, 168, 1, 177);
EthernetServer server(PORT);

String readString;
float debit = 0.00;                 /* Débit courant (L/mn). */
float volume = 0.00;                /* Volume d'eau dépensé. (Litre)*/
byte pulses = 0;
long Ts = 0;

bool synchro = false;               /* Synchro = true si configuration valide. Synchro = false alors pas d'arrosage */

long HMAvalue = 0;
String strHMA  = "20:30";           /* Heure d'arrosage en HH:MN */

long TPAvalue = 0;
String strTPA  = "05:00";           /* Temps d'arrosage */

long TPCvalue = 0;                  /* Temps(s) du PC */
long OldTPCvalue = 0;
String strTPC = "00:00";            /* Temps systeme */

long Tsysteme = 0;                  /* Temps(s) du Arduino */
char strTsysteme[9] = "00:00:00";

bool autorisationArrosage = true;   /* L'utilisateur inhibe l'arrosage */
bool refresh = false;

bool pompe_active  = false;
bool vanne_ouverte = false;

typedef enum { E_REPOS, E_ATTEND_HEURE, E_OUVERTURE_VANNE, E_DEMARRE, E_CONTROLE, E_ARRET_POMPE, E_FERMETURE_VANNE } E_AUTO;
E_AUTO EtatAutomate = E_REPOS;

const char *etape_nom[] = {
  "ARRET",
  "ATTENTE TOP DEPART",
  "OUVERTURE VANNE",
  "POMPE ACTIVE",
  "CONTROLE DEBIT",
  "ARRET POMPE",
  "FERME VANNE"
};

void InterruptSubRoutine () {
  /*-------------------------------------------------------------------------------*/
  pulses ++;
}

int automatisme () {
  /*-------------------------------------------------------------------------------*/
  if ( !synchro) return;
  if ( !autorisationArrosage ) return;

  switch ( EtatAutomate) {
    case E_REPOS : {
        if ( Tsysteme < HMAvalue ) {
          EtatAutomate = E_ATTEND_HEURE;
          if ( pompe_active ) {
            pompe_active = false;
            digitalWrite(PinPompe, HIGH);
          }
          if ( vanne_ouverte ) {
            vanne_ouverte = false;
            digitalWrite(PinVanne, HIGH);
          }
        }
      }
      break;

    case E_ATTEND_HEURE : {
        if ( Tsysteme > HMAvalue ) {
          Ts = (Tsysteme + T_OUVERTURE_VANNE);
          EtatAutomate = E_OUVERTURE_VANNE;
          if ( !vanne_ouverte ) {
            vanne_ouverte = true;
            digitalWrite(PinVanne, LOW);
          }
        }
      }
      break;

    case E_OUVERTURE_VANNE : {
        if ( Tsysteme > Ts) {
          Ts = (Tsysteme + T_MONTEE_EN_PRESSION);
          EtatAutomate = E_DEMARRE;
          if ( !pompe_active ) {
            pompe_active = true;
            digitalWrite(PinPompe, LOW);
          }
        }
      }
      break;

    case E_DEMARRE : {
        if ( Tsysteme > Ts) {
          Ts = (Tsysteme + TPAvalue);
          EtatAutomate = E_CONTROLE;
        }
      }
      break;

    case E_CONTROLE : {
#ifdef AVEC_DEBITMETRE
        if ( pulses >= NBPULSES_MIN) {
          if ( Tsysteme > Ts) {
            EtatAutomate = E_ARRET_POMPE;
          }
        }
        else {
          EtatAutomate = E_ARRET_POMPE;
        }
#else
        if ( Tsysteme > Ts) {
          EtatAutomate = E_ARRET_POMPE;
        }
#endif
        if ( EtatAutomate == E_ARRET_POMPE ) {
          Ts = (Tsysteme + T_DESCENTE_PRESSION);
        }
      }
      break;

    case E_ARRET_POMPE : {
        if ( pompe_active ) {
          pompe_active = false;
          digitalWrite(PinPompe, HIGH);
        }
        if ( Tsysteme > Ts ) {
          EtatAutomate = E_FERMETURE_VANNE;
          Ts = (Tsysteme + T_OUVERTURE_VANNE);
        }
      }
      break;

    case E_FERMETURE_VANNE : {
        if ( vanne_ouverte ) {
          vanne_ouverte = false;
          digitalWrite(PinVanne, HIGH);
        }

        if ( Tsysteme > Ts ) {
          EtatAutomate = E_REPOS;
        }
      }
      break;
  }
}

void setup() {
  /*-------------------------------------------------------------------------*/

  pinMode(PinPompe, OUTPUT);
  digitalWrite(PinPompe, HIGH);     /* Commande du relais POMPE    => Actif à l'état bas */

  pinMode(PinVanne, OUTPUT);
  digitalWrite(PinVanne, HIGH);  /* Commande du relais VANNE    => Actif à l'état bas */

  pinMode(PinDebitmetre, INPUT_PULLUP);      /* Entree en pull up */

  pinMode(PinDebug, OUTPUT);

#ifdef DBG
  // Open serial communications and wait for port to open:
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Arrosage setup");
#endif
  // start the Ethernet connection and the server:
  Ethernet.begin(mac, ip);

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
#ifdef DBG
    Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
#endif
    while (true) {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }
#ifdef DBG
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("Ethernet cable is not connected.");
  }
#endif

  pulses = 0 ;



  // initialiser le timer1 à 1 seconde
  noInterrupts(); // désactiver toutes les interruptions
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  OCR1A = 62500; // 16MHz/256/1Hz = 31250
  TCCR1B |= (1 << WGM12); // CTC mode
  TCCR1B |= (1 << CS12); // 256 prescaler
  TIMSK1 |= (1 << OCIE1A); // Activer le mode de comparaison

#ifdef DBG_PULSES
  // initialiser le timer2 à 67 pulses par secondes
  TCCR2A = 0;
  TCCR2B = 0;
  TCNT2 = 0;

  OCR2A = 116; // 16MHz/256/67Hz
  /*Mode CTC WGM22(TCCR2A)=0, WGM21(TCCR2A) = 1 et WGM20(TCCR2A) = 0 */
  TCCR2A |= (1 << WGM21); // CTC mode
  /* Pré diviseur par 1024 (Base 16MHz) : CS22 = 1, CS21 = 1 et CS20 = 1*/
  TCCR2B |= (1 << CS20); //  prescaler
  TCCR2B |= (1 << CS21); //  prescaler
  TCCR2B |= (1 << CS22); //  prescaler

  TIMSK2 |= (1 << OCIE2A); // Activer le mode de comparaison
#endif

  interrupts(); // activer toutes les interruptions


  // start the server
  server.begin();

#ifdef DBG
  Serial.print("server is at ");
  Serial.println(Ethernet.localIP());
#endif

  attachInterrupt(digitalPinToInterrupt(PinDebitmetre), InterruptSubRoutine, FALLING );
}

#define TPS_CYCLE_MESURE_CUVE_s   10
#define CoefficientMetriqueRange  3.096
#define hauteurDeLaCuve_m         1.60

int TempsCycleMesureHauteurCuve = 0;
double hauteurEau_cm = 0.0;

ISR(TIMER1_COMPA_vect) { // fonction périodique 1s
  /*-------------------------------------------------------------------------*/
  //digitalWrite(PinDebug, digitalRead(PinDebug) ^ 1); // Basculer la LED allumée/éteinte
  tempsSysteme ();
  automatisme ();
  pulses = 0;
  if ( TempsCycleMesureHauteurCuve == 0 ) {
    TempsCycleMesureHauteurCuve  = TPS_CYCLE_MESURE_CUVE_s ;
    double hBrute = CoefficientMetriqueRange * analogRead (A0) / 1024.0;
    hauteurEau_cm =  (hauteurDeLaCuve_m - hBrute) * 100 ;
    hauteurEau_cm += 5.5 ; // Correction de la hauteur (en cm).
    //hauteurEau = (double) analogRead (A0);
    if ( hauteurEau_cm < 0 ) hauteurEau_cm = 0;
  }
  else TempsCycleMesureHauteurCuve --;
}

#ifdef DBG_PULSES
ISR(TIMER2_COMPA_vect) {// fonction périodique  1 ms
  /*-------------------------------------------------------------------------*/
  digitalWrite(PinDebug, digitalRead(PinDebug) ^ 1); // Basculer la LED allumée/éteinte
}
#endif

int tempsSysteme () {
  /*-------------------------------------------------------------------------*/
#if 1
  if ( OldTPCvalue != TPCvalue )  {

    Tsysteme = TPCvalue ;
    OldTPCvalue = TPCvalue;
    synchro = true;
#ifdef DBG
    Serial.print("MAJ TPCvalue= "); Serial.println(TPCvalue);
    Serial.print("MAJ Tsysteme= "); Serial.println(Tsysteme);
#endif
  }
#endif

  debit = (float)pulses / COEFFICIENT_DEBITMETRE;

  volume += (debit / 60.0) ;
  Tsysteme ++;
  // Serial.print("Tsystem= "); Serial.print(Tsysteme);Serial.print(" TPCvalue= "); Serial.print(TPCvalue);    Serial.print(" oldTPCvalue= "); Serial.println(OldTPCvalue);
  if ( Tsysteme > 86399) {
    Tsysteme = 0;
    //Serial.println("RAZ Tsysteme");
  }

  long heure = Tsysteme / 3600;
  long minutes = (Tsysteme - heure * 3600) / 60 ;
  long seconds = Tsysteme - heure * 3600 - minutes * 60;
  sprintf( strTsysteme, "%02d:%02d:%02d", (byte)heure, (byte)minutes, (byte)seconds);
  //Serial.print("heure:");Serial.print(heure);Serial.print(" minutes:");Serial.print(minutes);Serial.print(" seconds:");Serial.print(seconds);Serial.print(" format:");Serial.println(strDate);
  // }
}

void loop() {
  /*----------------------------------------------------------------------------
  */

  EthernetClient client = server.available();
  if (client) {
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();

        if (readString.length() < 100) {
          readString += c;
        }

        //if HTTP request has ended
        if (c == '\n') {
#ifdef DBG
          Serial.println(readString);
#endif
          //now output HTML data header

          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println();

          if ( readString.indexOf ("HeureSysteme") > 1) {
            if (( readString.indexOf ("HH=&") > 1) || ( readString.indexOf ("HH=%") > 1)) {
              // Rien

#ifdef DBG
              Serial.println("HeureSysteme rien");
#endif
            }
            else if ( readString.indexOf ("HH") > 1) {
              int pos = readString.indexOf ("HH");
              String c = readString.substring (pos + 3, pos + 5 );
              String d = readString.substring (pos + 8, pos + 10 );
              TPCvalue = c.toInt() * 3600 + d.toInt() * 60 ;

#ifdef DBG
              Serial.print("TPCvalue="); Serial.print(strTPC); Serial.print(" "); Serial.println(TPCvalue);
#endif
            }
          }

          if ( readString.indexOf ("Configurer") > 1) {

            if ( readString.indexOf ("HMA") > 1) {
              int pos = readString.indexOf ("HMA");
              String c = readString.substring (pos + 4, pos + 6 );
              String d = readString.substring (pos + 9, pos + 11 );
              strHMA = c + ":" + d;
              HMAvalue = c.toInt() * 3600 + d.toInt() * 60;
              //Serial.println(HMAvalue);
            }

            if ( readString.indexOf ("TPA") > 1) {
              int pos = readString.indexOf ("TPA");
              String c = readString.substring (pos + 4, pos + 6 );
              String d = readString.substring (pos + 9, pos + 11 );
              strTPA = c + ":" + d;
              TPAvalue = c.toInt() * 60 + d.toInt();
              //Serial.println(TPAvalue);
            }
            if (readString.indexOf("ACA") > 1) {
              autorisationArrosage = true;
            }
            else {
              autorisationArrosage = false;
            }
            //Serial.println(autorisationArrosage);
          }
          if ( readString.indexOf ("Rafraichir") > 1) {
            refresh = true;
          }

          if ( readString.indexOf ("Arreter") > 1) {
            refresh = false;
          }

          if ( readString.indexOf ("ResetVolume") > 1) {
            volume = 0;
          }


          //--------------------------- PAGE HTML ----------------------------------

          client.println("<HTML>");
          client.println("<HEAD>");
          client.println("<TITLE>Arrosage</TITLE>");
          if ( refresh ) client.println("<meta http-equiv=""\"refresh""\" content=""\"1""\" />");
          client.println("</HEAD>");
          client.println("<BODY style=""background-color:powderblue"">");

          client.println("<H1 style=""color:blue"">Arrosage automatique</H1>");

          client.println("<FORM ACTION='/' method=get >");

          client.println("<H2 style=""background-color:#6cc3f9;"">Mise a l'heure</H2>");
          client.println(F("<p id=""\"DATEHEURE""\"></p><script>var d = new Date();document.getElementById(""\"DATEHEURE""\").innerHTML = d;</script>"));

          // client.println(F("<onload=loadImage()><script>function loadImage() {alert(""Image is loaded"");}</script>"));

          client.println(F("<p><label for=""HH"">Heures.</label><input type=""time"" id=""HH"" name= ""HH"" </p><script>var date = new Date();document.getElementById(""\"HH""\").innerHTML = date.getHours();</script>"));
          client.println("<INPUT TYPE=SUBMIT NAME='submit' VALUE='HeureSysteme'>");

          client.println("<H2 style=""background-color:#6cc3f9;"">Configuration</H2>");
          client.println("<hr />");

          client.print(F("<p><label for=""HMA"">Heure d'arrosage.</label><input type=""time"" id=""HMA"" name= ""HMA"" value="));  client.print(strHMA); client.println(F(" /></p>"));
          client.println(F("<p><label for=""TPA"">Duree d'arrosage.</label><input type=""time"" id=""TPA"" name=""TPA"" Min =""01:00"" Max =""10:00"" value =")); client.print(strTPA); client.println(F(" /></p>"));
          client.println(F("<p><label for=""ACA"">Autoriser le cycle d'arrosage.</label><input type=""checkbox"" id=""ACA"" name=""ACA" ));
          if (autorisationArrosage) client.print(" Checked"); client.println(F(" /></p>"));
          client.println("<INPUT TYPE=SUBMIT NAME='submit' VALUE='Configurer'>");

          client.println("<hr />");
          client.println("<H2 style=""background-color:#6cc3f9;"">Mesures</H2>");

          client.print(F("<p><label for=""HSY"">Heure systeme.\t</label><span id=""HSY"" name=""HSY"" disabled>"));     client.print(strTsysteme);  client.println("</span></p>");
          client.print(F("<p><label for=""DEB"">Debit (L/mn).\t</label> <span id=""DEB"" name=""DEB"" disabled>"));     client.print(debit);        client.println("</span></p>");
          client.print(F("<p><label for=""VOL"">Volume (L).\t</label><span id=""VOL"" name=""VOL"" disabled>"));        client.print(volume);       client.print("</span></p>");
          client.print(F("<p><label for=""HDO"">Hauteur Eau (cm).\t</label><span id=""HDO"" name=""HDO"" disabled>"));   client.print(hauteurEau_cm);   client.print("</span></p>");         
          
          client.println("<INPUT TYPE=SUBMIT NAME='submit' VALUE='ResetVolume'>");
          client.println("<hr />");
          client.print(F("<p><label for=""STATE"">Etat : \t</label><span id=""STATE"" name=""STATE"" disabled>")); client.print(etape_nom[EtatAutomate]);   client.println("</span></p>");
          client.println("<hr />");
          if ( !refresh ) client.println("<INPUT TYPE=SUBMIT NAME='submit' VALUE='Rafraichir'>");
          else client.println("<INPUT TYPE=SUBMIT NAME='submit' VALUE='Arreter'>");
          client.println("</FORM>");
          client.println("</BODY>");
          client.println("</HTML>");

          readString = "";  /* RAZ buffer */

          delay(1);
          client.stop();
        }
      }
    }
  }
}
