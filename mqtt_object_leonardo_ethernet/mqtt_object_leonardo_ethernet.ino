#include <UIPEthernet.h>
#include <Vector.h>
#include <PubSubClient.h>

enum DeviceType {button, rele};

class Device {
  public:
    byte pin;            // номер вывода
    byte mode = INPUT;  // режим работы
    // (используем так как по умолчанию RTTI диактивирован и влечёт большые накладные расходы и нет возможности использовать typeid)
    DeviceType type;   // ключ для определения типа к которому будем приводить
    const char* name; // Имя устройства
    int state = 0; // Состояние устройства
};

class Button : public Device {
  public:
    Button(const char* _name, byte _pin);
};

class Rele : public Device {
  public:
    Rele(const char* _name, byte _pin);
};

class MQTT_Device {
  public:
    const char* topic_module; // топик с названием модуля
    const char* topic_room; // топик с именем комнаты
    const char* topic_device; // топик с названием устройства (кнопка, реле, датчик и.т.д)
    Device *device; // ссылка на устройство
    MQTT_Device(const char* _topic_module, const char* _topic_room,  const char* _topic_device,  Button *_device);
    MQTT_Device(const char* _topic_module, const char* _topic_room,  const char* _topic_device,  Rele *_device);
};

class WiFi_Setting {
  public:
    const char* ssid;
    const char* password;

    WiFi_Setting(const char* ssid, const char* password);
};

WiFi_Setting::WiFi_Setting(const char* _ssid, const char* _password) {
  ssid = _ssid;
  password = _password;
};

Button::Button(const char* _name, byte _pin) {
  name = _name;
  state = 0;
  type = button;
  pin = _pin;
  mode = INPUT;
}

Rele::Rele(const char* _name, byte _pin) {
  name = _name;
  state = 0;
  type = rele;
  pin = _pin;
  mode = OUTPUT;
}

MQTT_Device::MQTT_Device(const char* _topic_module, const char* _topic_room, const char* _topic_device,  Button *_device)
{
  topic_module = _topic_module;
  topic_room = _topic_room;
  topic_device = _topic_device;
  device = _device;
}

MQTT_Device::MQTT_Device(const char* _topic_module, const char* _topic_room, const char* _topic_device,  Rele *_device)
{
  topic_module = _topic_module;
  topic_room = _topic_room;
  topic_device = _topic_device;
  device = _device;
}

// определяем конфигурацию сети
byte mac[] = {0xAE, 0xB2, 0x26, 0xE4, 0x4A, 0x5C}; // MAC-адрес
byte ip [] = {192, 168, 88, 120};

EthernetClient ethClient; // объект клиент
PubSubClient client(ethClient);

const char* mqtt_server = "192.168.88.253";
const char* mqtt_login = "";
const char* mqtt_password = "";
Vector<MQTT_Device> mqtt_list;

long lastMsg = 0;
char msg[50];

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("Init device");
  mqtt_list.push_back(MQTT_Device("/leonardo", "/Bathroom", "/button1", reinterpret_cast<Button*>(new Button("Выключатель 1", 11))));
  mqtt_list.push_back(MQTT_Device("/leonardo", "/Bathroom", "/button2", reinterpret_cast<Button*>(new Button("Выключатель 2", 12))));
  mqtt_list.push_back(MQTT_Device("/leonardo", "/Bathroom", "/rele1", reinterpret_cast<Rele*>(new Rele("Реле 1", 13))));
  mqtt_list.push_back(MQTT_Device("/leonardo", "/Bathroom", "/rele2", reinterpret_cast<Rele*>(new Rele("Реле 2", 14))));
  
//  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  for (int i = 0; i < mqtt_list.size(); i++){
    pinMode(mqtt_list[i].device->pin, mqtt_list[i].device->mode);
  }
  setup_ethernet();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}


const String catStr(const char* str1, const char* str2) {
  char result[512];
  snprintf(result, sizeof result, "%s%s", str1, str2);
  String str = result;
  return str;
}

void setup_ethernet() {
  Ethernet.begin(mac,ip); // инициализация контроллера
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Ищем топик реле (его будем щёлкать если пришло изменение)
  int releNumber = -1;
  for (int i = 0; i < mqtt_list.size(); i++){
    if (mqtt_list[i].device->type == rele) {
      Rele *dev = reinterpret_cast<Rele*>(mqtt_list[i].device);
      String base = catStr(mqtt_list[i].topic_module,mqtt_list[i].topic_room);
      String dev_name(mqtt_list[i].topic_device);
      if (strcmp(topic,(base + dev_name).c_str())==0) releNumber = i;
    }
    if (releNumber > -1) break;
  }
  if (releNumber > -1) {
    if ((char)payload[0] == '1') setstate(releNumber,1);
    if ((char)payload[0] == '0') setstate(releNumber,0);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_login, mqtt_password)) {
      Serial.println("connected");
      for (int i = 0; i < mqtt_list.size(); i++) {
        String base = catStr(mqtt_list[i].topic_module,mqtt_list[i].topic_room);
        Serial.print("Подписываемся на топик : ");
        Serial.println(catStr(base.c_str(),mqtt_list[i].topic_device).c_str());
        client.subscribe(catStr(base.c_str(),mqtt_list[i].topic_device).c_str());
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Опрашиваем устройства (и публикуем состояния в MQTT брокера)
  for (int i = 0; i <  mqtt_list.size(); i++) {
    switch (mqtt_list[i].device->type) {
      case button:
      // По состоянию выключателей переключаем реле и за одно передаём их состояние
      // Всё это в Action
      // (как и раньше только передаётся еще и состояние выключателей) 
      // можно судить будет на стороне Mojordomo кто щёлкнул реле (реальный выключатель или на стороне Mojerdomo программное переключение произошло)
        if (PubButtonState(&mqtt_list[i]) == true) Action(i);
        break;
      case rele:
      // Постоянно публиковать походу не нужно, дальше разберёмся может добавлю предыдущее состояние реле и буду публиковать по изменению значения
//        PubReleState(&mqtt_list[i]);
        break;
    }
  }
}

void setstate(int id, int state) {
  if (state == 0) {
    digitalWrite(mqtt_list[id].device->pin,HIGH); 
    mqtt_list[id].device->state = 0;
  } else {
    digitalWrite(mqtt_list[id].device->pin,LOW);
    mqtt_list[id].device->state = 1;
  }
}

// Переключает состояние устройства на противоположное
int swithState(int id){
   if (mqtt_list[id].device->state == 0){ 
    setstate(id,1);
    return 1;
   } else {
    setstate(id,0);
    return 0;
   }
}

// Некий экшен в качестве ID принемаем номер Device , эта функция всегда специфичная
// Пока не очень красиво всё привязано к номерам , но некий HashMap я пока хз как реализовать
void Action(int id){
  switch (id) {
     case 0: // Если был счёлчок button1
         swithState(2);
         PubReleState(&mqtt_list[2]);
     break;
     case 1: // Если был счёлчок button2
         swithState(3);
         PubReleState(&mqtt_list[3]);
     break;
  }
}

// Если состояние Кнопки изменилось публикует это дело в MQTT сеть (и возвращает true если менялось положение)
boolean PubButtonState(MQTT_Device *mqtt_dev){
  MQTT_Device *mqttdev = reinterpret_cast<MQTT_Device*>(mqtt_dev);
  Button *dev = reinterpret_cast<Button*>(mqttdev->device);
  int current_rele_state = digitalRead(dev->pin);
  if (current_rele_state != dev->state) {
    String base = catStr(mqtt_dev->topic_module,mqtt_dev->topic_room);
    snprintf (msg, 75, "%ld", dev->state);
    Serial.print("Publish message: ");
    Serial.print(catStr(base.c_str(),mqtt_dev->topic_device).c_str()); Serial.print(" - ");
    Serial.println(msg);
    dev->state = current_rele_state;
    client.publish(catStr(base.c_str(),mqtt_dev->topic_device).c_str(), msg);
    return true;
  }
  return false;
}

// Публикуем текущее состояние реле
void PubReleState(MQTT_Device *mqtt_dev){
  MQTT_Device *mqttdev = reinterpret_cast<MQTT_Device*>(mqtt_dev);
  Rele *dev = reinterpret_cast<Rele*>(mqttdev->device);
  String base = catStr(mqtt_dev->topic_module,mqtt_dev->topic_room);
  snprintf (msg, 75, "%ld", dev->state);
  Serial.print("Publish message: ");
  Serial.print(catStr(base.c_str(),mqtt_dev->topic_device).c_str()); Serial.print(" - ");
  Serial.println(msg);
  client.publish(catStr(base.c_str(),mqtt_dev->topic_device).c_str(), msg);
}

