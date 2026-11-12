# asvJSON++

Библиотека C++17 для работы с JSON, поддерживающая бинарные данные, DateTime, Base64, BSON, MessagePack, CBOR, JSON Pointer, JSON Merge Patch, XML, YAML, CSV, TOON, TRON, GOON, ADE, Protobuf, TOML, JSON Lines (NDJSON), S-Expression, SVG, валидацию по JSON Schema, JSONPath (RFC 9535), SAX/потоковое парсинг JSON и генерацию C++ struct/enum.

**Автор:** Сергей Андык asvzzz@narod.ru

## Онлайн-конвертер

Попробуйте онлайн: **https://jx8.ru/** — браузерный конвертер, поддерживающий все 26 форматов, с загрузкой и скачиванием файлов.

## Лицензия

MIT license — см. файл `LICENSE` для подробностей.

## Возможности

### Ядро
- Типы JSON: String, Number, Boolean, Null, Object, Array
- Бинарные данные с кодировкой Base64
- Пользовательский алфавит Base64 для шифрования/обфускации
- DateTime ISO-8601 с миллисекундами
- Работа с файлами
- Автоопределение формата при чтении/записи: `readFromFileAny()` / `writeToFileAny()`
- Безопасные доступы с дефолтными значениями (`opt*`)
- Поддержка `std::string_view` для всех ключей и строк (zero-copy где возможно)
- `noexcept` на всех аксессорах (`getInt`, `getDouble`, `getBool`) для производительности
- Порядок ключей сохраняется при парсинге → сериализации: parse -> serialize сохраняет порядок ключей в JSON, YAML, INI и всех форматах; опционально `ASVJSON_USE_ORDERED_MAP` переключает на сортируемый `std::map`
- `std::from_chars` для быстрого парсинга double на C++23 компиляторах
- Флаг `allowNaNInfinity` — парсинг/сериализация NaN и Infinity значений
- `operator[]` для цепочечной навигации (`json["a"]["b"]["c"]`)
- Итераторный API: `begin()`/`end()` для объектов и массивов, помощник `values()`
- `emplace(key, val)` и `push(val)` для конструирования значений на месте
- `operator==` / `operator!=` для глубокого структурного сравнения
- `merge(other)` — глубокое слияние по RFC 7396 с удалением null
- `diff(other)` — генерация массивов JSON Patch
- Определение формата: `getFormatType()` и `fromAny()` с эвристическим определением по содержимому

### Комментарии
- Стили `//`, `/* */`, `#`

### Вложенный доступ
- Точечная нотация: `json.getNested("user.address.city")`
- Экранирование точек обратной косой: `json.getNested("domain\\.example.com")`

### Форматы сериализации
- JSON (компактный и форматированный)
- BSON (бинарный) — формат документов MongoDB; дублирующиеся ключи по правилу last-wins; строковые элементы должны заканчиваться NUL; неподдерживаемые типы (например `CUSTOM_TAG`) отвергаются с ошибкой
- MessagePack (бинарный, RFC 7049)
- CBOR — `toCBOR()` / `fromCBOR()` — RFC 8949 Concise Binary Object Representation с неопределённой длиной элементов, float16, тегами datetime/extension/regex
- YAML — `toYAML()` / `fromYAML()` с блочными последовательностями/отображениями, литералами YAML 1.2 `.nan`/`.inf`, автоматическим кавычением ключей, буквальными блочными скалярами, тегами (`!!int`/`!!float`/`!!bool`/`!!null`/`!!str`/`!!set`/`!!omap`/`!!pairs`/`!!value`/`!!yaml`), блочным `!!set` (`- key` / `? key` элементы), явными тегами на блочных узлах, сложными (flow-collection) ключами отображения, пользовательскими тегами как `CUSTOM_TAG`, настраиваемым сериализатором (`YamlWriteOptions`: flow/block/auto стиль, ширина отступа, контроль кавычек), якорями и алиасами, мульти-документными потоками, директивами `%TAG`/`%YAML`, ошибками с номером строки
- XML — `toXML()` / `fromXML()` — сериализация и парсинг с атрибутами (`@`-префикс), текстовым содержимым (`#text`), CDATA, комментариями, определением типов (datetime/binary/objectid/regex/timestamp/extension); некорректный вход отвергается
- CSV — `toCSV()` с рекурсивным flattening (`a.b.c`) и порядком столбцов по первому появлению
- TOON — `toTOON()` / `fromTOON()` — токен-ориентированная нотация объектов с inline и табличными массивами
- TRON — `toTRON()` / `fromTRON()` — token-reduced object notation с определениями классов, наследованием, именованными аргументами
- GOON — `toGOON()` / `fromGOON()` — greatly optimized object notation с YAML-подобной индентацией, табличными массивами, односимвольными литералами (T/F/_/~), ссылками на словари
- Protobuf — `toProtobuf()` / `fromProtobuf()` — бинарный wire-формат Protocol Buffers со схемо-управляемым отображением полей; плюс `toProtobufText()` / `fromProtobufText()` для читаемого текстового формата
- TOML — `toTOML()` / `fromTOML()` — Tom's Obvious Minimal Language с таблицами, массивами таблиц, inline таблиц/массивов, многострочными строками
- JSON Lines (NDJSON) — `toJSONLines()` / `fromJSONLines()` — одно JSON значение на строку, идеально для стриминга/логов/пакетных операций
- S-Expression — `toSexpr()` / `fromSexpr()` — Lisp-стиль вложенных списков `(key "value" (nested 1 2 3))` с эвристическим определением объект/массив, `;` комментариями, `nil`/`#t`/`#f` литералами
- JSON5 — `toJSON5()` / `fromJSON5()` — надмножество JSON с некейвированными ключами, строками в одинарных кавычках, trailing запятыми, hex/octal/binary числами, leading десятичной точкой, знаком плюс, комментариями, NaN/Infinity литералами
- INI — `toINI()` / `fromINI()` — классический формат файлов конфигурации с секциями, парами ключ-значение, `;`/`#` комментариями, кавычкованными значениями, escape-последовательностями, продолжением строки
- Dotenv — `toDotenv()` / `fromDotenv()` — плоские файлы окружения `KEY=VALUE` с опциональным префиксом `export`, `#` комментариями, одинарными и двойными кавычками
- UBJSON — `toUBJSON()` / `fromUBJSON()` — Universal Binary JSON (ubjson.org), big-endian маркеры для int8/uint8/int16/int32/int64, float32/float64, strings (`S`), null/bool, однородные типизированные контейнеры
- Bencode — `toBencode()` / `fromBencode()` — кодировка BitTorrent (целые числа, байтовые строки, списки, сортированные по ключам словари)
- JavaProp — `toJavaProp()` / `fromJavaProp()` — классические Java `.properties` файлы: `#`/`!` комментарии, `=`/`:` разделители, `\` продолжение строки
- Plist — `toPlist()` / `fromPlist()` — классический Apple XML property list (`<plist>`/`<dict>`/`<array>` обёртка)
- ADE — `toADE()` / `fromADE()` — Advanced Data Exchange с заголовком `# ADE`, мульти-документами, якорями/алиасами, блочными скалярами `|`/`>`, тегами типов (`!int`, `!float`, `!string`, `!bool`, `!null`, `!schema`, `!base64`, `!bin`, `!datetime`, `!ext`, `!regex`)
- RON — `toRON()` / `fromRON()` — Rusty Object Notation (формат crates `ron`) со структурами, кортежами, картами, опционалами `Some`/`None`, перечислениями, диапазонами
- C++ codegen — `toCpp()` / `fromCpp()` — генерация C++17 `struct`/`enum` объявлений из дерева значений и парсинг обратно
- SVG — `toSVG()` / `fromSVG()` — Scalable Vector Graphics, XML-основанный формат векторной графики

### Стандарты
- JSON Pointer (RFC 6901)
- JSON Merge Patch (RFC 7396)
- JSON Patch (RFC 6902)
- Валидация по JSON Schema (draft-07)
- JSONPath (RFC 9535)

## Ограничения

- MAX_NESTING_DEPTH: 48
- MAX_STRING_LEN: 10 MB
- MAX_ARRAY_SIZE: 1 000 000
- MAX_OBJECT_SIZE: 1 000 000

## Быстрый старт

```cpp
#include "asvJSON++.hpp"

int main() {
    asvJSON json;
    json.parse("{}");

    json.putString("name", "John");
    json.putInt("age", 30);
    json.putBool("enabled", true);
    json.putDouble("balance", 1234.56);

    std::string compact = json.serialize();
    std::string pretty = json.serialize(true);

    json.parse(compact);
    std::string name = json.getString("name");

    // Доступ к вложенным значениям
    json.parse(R"({"user":{"address":{"city":"New York"}}})");
    std::string city = json.getString("user.address.city");

    // JSON Pointer
    auto* val = json.getByPointer("/user/address/city");
    std::string_view sv = val->getStringView();

    // Конвертация в другие форматы
    std::string xml = json.toXML();
    std::string yaml = json.toYAML();
    std::string csv = json.toCSV();
    std::string tron = json.toTRON();
    std::string sexpr = json.toSexpr();

    return 0;
}
```

## API Справочник

### Класс asvJSON

#### Конструкторы и парсинг

| Метод | Описание |
|-------|----------|
| `asvJSON()` | Конструктор по умолчанию. |
| `asvJSON(const asvJSON& other)` | Конструктор копирования (глубокая копия). |
| `asvJSON(asvJSON&& other) noexcept` | Конструктор перемещения. |
| `~asvJSON()` | Деструктор. |
| `asvJSON& operator=(const asvJSON& other)` | Присваивание копированием. |
| `asvJSON& operator=(asvJSON&& other) noexcept` | Присваивание перемещением. |
| `bool parse(std::string_view json)` | Парсинг JSON. Принимает `std::string`, строковые литералы, `const char*`. UTF-8 BOM (`EF BB BF`) в начале автоматически удаляется. |
| `void clear()` | Очистка всех данных, сброс в пустое состояние. |
| `std::string getLastError() const` | Возврат сообщения об последней ошибке. |
| *(поле)* `bool allowNaNInfinity` | При `true` — парсинг/сериализация `NaN`, `Infinity`, `-Infinity` (нестандартный JSON). По умолчанию `false`. |

#### Сериализация и работа с файлами

| Метод | Описание |
|-------|----------|
| `std::string serialize(bool pretty = false) const` | Сериализация в JSON строку. |
| `size_t byteSize() const` | Размер в байтах сериализованного документа. **ПРЕДУПРЕЖДЕНИЕ:** выполняется полная сериализация (выделение всей выходной строки) только для подсчёта байтов. Для больших документов вызовите `serialize()` один раз и используйте `.size()` возвращённой строки. |
| `std::string toXML() const` | Сериализация в XML документ. |
| `bool fromXML(std::string_view input)` | Парсинг XML строки. |
| `std::string toYAML() const` | Сериализация в YAML документ (блочный стиль). |
| `bool fromYAML(std::string_view input)` | Парсинг YAML строки. |
| `std::string toCSV() const` | Сериализация в CSV. |
| `std::string toTOON() const` | Сериализация в TOON. |
| `bool fromTOON(std::string_view input)` | Парсинг TOON строки. |
| `std::string toTRON() const` | Сериализация в TRON. |
| `bool fromTRON(std::string_view input)` | Парсинг TRON строки. |
| `std::string toGOON() const` | Сериализация в GOON. |
| `bool fromGOON(std::string_view input)` | Парсинг GOON строки. |
| `std::string toADE(bool strict = false) const` | Сериализация в ADE. |
| `bool fromADE(std::string_view input, bool strict = false)` | Парсинг ADE строки. |
| `std::vector&lt;uint8_t&gt; toProtobuf(const std::string&amp; schema) const` | Сериализация в бинарный wire-формат Protocol Buffers. |
| `bool fromProtobuf(const uint8_t* data, size_t size, const std::string&amp; schema)` | Парсинг бинарного wire-формата Protocol Buffers. |
| `std::string toProtobufText() const` | Сериализация в текстовый формат Protobuf. |
| `bool fromProtobufText(const std::string&amp; text)` | Парсинг текстового формата Protobuf. |
| `std::string toTOML() const` | Сериализация в TOML. |
| `bool fromTOML(std::string_view input)` | Парсинг TOML строки. |
| `std::string toJSONLines() const` | Сериализация в JSON Lines. |
| `bool fromJSONLines(std::string_view input)` | Парсинг JSON Lines текста в массив значений. |
| `std::string toSexpr() const` | Сериализация в S-Expression. |
| `bool fromSexpr(std::string_view input)` | Парсинг S-Expression строки. |
| `std::string toRON(bool pretty = true) const` | Сериализация в RON. |
| `bool fromRON(std::string_view input)` | Парсинг RON строки. |
| `std::string toCpp(const CppWriteOptions& opts = {}) const` | Генерация C++ `struct`/`enum` объявлений из дерева значений. |
| `bool fromCpp(std::string_view input, const CppReadOptions& opts = {})` | Парсинг C++ `struct`/`enum` объявлений в дерево значений. |
| `std::string toDotenv() const` | Сериализация в Dotenv (`.env`). |
| `bool fromDotenv(std::string_view input)` | Парсинг Dotenv строки. |
| `std::string toBencode() const` | Сериализация в Bencode. |
| `bool fromBencode(std::string_view input)` | Парсинг Bencode строки. |
| `std::string toJavaProp() const` | Сериализация в Java `.properties`. |
| `bool fromJavaProp(std::string_view input)` | Парсинг Java `.properties` строки. |
| `std::string toPlist() const` | Сериализация в Apple XML property list. |
| `bool fromPlist(std::string_view input)` | Парсинг Apple XML property list. |
| `std::vector<uint8_t> toUBJSON() const` | Сериализация в UBJSON. |
| `bool fromUBJSON(const void* data, size_t size)` | Парсинг UBJSON из сырых байтов. |
| `bool fromUBJSON(const std::string& data)` | Парсинг UBJSON из `std::string`. |
| `bool writeToFile(const std::string& filename, bool pretty = false) const` | Запись JSON в файл. |
| `bool readFromFile(const std::string& filename)` | Чтение и парсинг JSON из файла. |

#### Установка значений (Object / Root)

Все методы `put*` создают корень как объект, если он не существует.  
Ключи могут быть `const char*`, `std::string` или `std::string_view`.

| Метод | Описание |
|-------|----------|
| `void putString(key, value)` | Добавить строку. |
| `void putInt(key, int64_t value)` | Добавить 64-битное целое. |
| `void putDouble(key, double value)` | Добавить double. |
| `void putFloat32(key, float value)` | Добавить float (хранится как double). |
| `void putBool(key, bool value)` | Добавить boolean. |
| `void putNull(key)` | Добавить JSON null. |
| `void putDateTime(key, time_t value)` | Добавить datetime (секунды с epoch). |
| `void putBinary(key, const uint8_t* data, size_t len)` | Добавить бинарные данные. |
| `void putObjectId(key, std::string_view oid)` | Добавить ObjectId (12 байт). |

#### Получение значений (Object / Nested)

| Метод | Возвращаемый тип | Описание |
|-------|-----------------|----------|
| `getString(key)` | `std::string` | Строковое значение. |
| `getStringView(key)` | `std::string_view` | Zero-copy view строки. |
| `getInt(key)` | `int64_t` | Целое значение. |
| `getDouble(key)` | `double` | Double значение. |
| `getBool(key)` | `bool` | Boolean значение. |
| `getDateTime(key)` | `time_t` | Datetime (секунды). |
| `getDateTimeString(key)` | `std::string` | ISO-8601 строка. Возвращает `""` если timestamp не может быть конвертирован в UTC. |
| `getBinary(key)` | `std::vector<uint8_t>` | Бинарные данные. |
| `getObjectId(key)` | `std::string` | ObjectId как 12-байтовая строка. |

#### Проверки типа

| Метод | Описание |
|-------|----------|
| `isNull(key)` | Проверка на JSON null. |
| `isString(key)` | Проверка на строку. |
| `isInt(key)` | Проверка на целое число. |
| `isDouble(key)` | Проверка на double. |
| `isBool(key)` | Проверка на boolean. |
| `isObject(key)` | Проверка на объект. |
| `isArray(key)` | Проверка на массив. |
| `isBinary(key)` | Проверка на бинарные данные. |
| `isDateTime(key)` | Проверка на datetime. |
| `isObjectId(key)` | Проверка на ObjectId. |

#### Операции с массивами

| Метод | Описание |
|-------|----------|
| `void arrayAddString(key, const char* value)` | Добавить строку в массив. |
| `void arrayAddInt(key, int64_t value)` | Добавить целое в массив. |
| `void arrayAddDouble(key, double value)` | Добавить double в массив. |
| `void arrayAddBool(key, bool value)` | Добавить boolean в массив. |
| `void arrayAddNull(key)` | Добавить null в массив. |

#### JSON Pointer (RFC 6901)

| Метод | Описание |
|-------|----------|
| `getByPointer(std::string_view pointer)` | Поиск значения по JSON Pointer (`/user/address/city`). |
| `setByPointer(std::string_view pointer, asvJSONValue* value)` | Установка значения по пути указателя. |
| `removeByPointer(std::string_view pointer)` | Удаление значения по пути указателя. |

#### Слияние / Патчи

| Метод | Описание |
|-------|----------|
| `void merge(const asvJSON& other)` | Поверхностное слияние ключей объекта. |
| `bool applyPatch(const asvJSON& patch)` | JSON Patch (RFC 6902) — массив операций. |
| `asvJSON applyMergePatch(const asvJSON& patch) const` | JSON Merge Patch (RFC 7396) — возвращает новый документ. |

#### Бинарная конвертация форматов

| Метод | Описание |
|-------|----------|
| `std::vector<uint8_t> toMessagePack() const` | Сериализация в MessagePack. |
| `bool fromMessagePack(const uint8_t* data, size_t size)` | Парсинг MessagePack из сырых байтов. |
| `std::vector<uint8_t> toCBOR() const` | Сериализация в CBOR (RFC 8949). |
| `bool fromCBOR(const uint8_t* data, size_t size)` | Парсинг CBOR из сырых байтов. |
| `std::vector<uint8_t> toBSON() const` | Сериализация в BSON. |
| `bool fromBSON(const uint8_t* data, size_t size)` | Парсинг BSON из сырых байтов. |

### Класс asvJSONValue

#### Статические фабричные методы

| Метод | Описание |
|-------|----------|
| `makeString(const char* s, size_t len)` | Создать строковое значение. |
| `makeInt(int64_t n)` | Создать 64-битное целое. |
| `makeDouble(double d)` | Создать double. |
| `makeBool(bool b)` | Создать boolean. |
| `makeNull()` | Создать null значение. |
| `makeObject()` | Создать пустой объект. |
| `makeArray()` | Создать пустой массив. |
| `makeDateTime(time_t ts, int ms = 0)` | Создать datetime. |
| `makeBinary(const uint8_t* data, size_t len)` | Создать бинарные данные. |
| `makeObjectId(std::string_view oid)` | Создать ObjectId (12 байт). |

## Примеры использования

### XML

Вход:
```json
{"name":"John & Jane","active":true,"items":[1,2,3]}
```

Выход:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<root>
  <active>true</active>
  <items>
    <item>1</item>
    <item>2</item>
    <item>3</item>
  </items>
  <name>John &amp; Jane</name>
</root>
```

### YAML

`toYAML()` сериализует в YAML с блочными последовательностями и именованными блоками.  
`fromYAML()` парсит YAML обратно в JSON.

Вход:
```json
{"name":"John","items":[1,2,3],"meta":{"nested":true}}
```

Выход (`toYAML`):
```yaml
---
name: John
items:
  - 1
  - 2
  - 3
meta:
  nested: true
```

### CBOR (RFC 8949)

CBOR — бинарный надмножество JSON, стандартизированное как RFC 8949.

```cpp
asvJSON json;
json.parse(R"({"name":"Test","count":42,"active":true})");
auto cbor = json.toCBOR();
// cbor.size() < json.serialize().size() — компактный бинарный формат

asvJSON json2;
json2.fromCBOR(cbor.data(), cbor.size());
```

### CSV

Вход:
```json
{"name":"John","age":30,"meta":{"city":"NYC"}}
```

Выход:
```
name,age,meta.city
John,30,NYC
```

Вложенные объекты flattenятся в точечные имена столбцов (`meta.city`). Порядок столбцов — по первому появлению каждого ключа.

### TRON

TRON (Token Reduced Object Notation) — надмножество JSON с определениями классов для переиспользуемых структур.

Вход:
```json
[{"name":"Alice","age":30},{"name":"Bob","age":25}]
```

Выход:
```tron
class A: name,age
[A(Alice,30),A(Bob,25)]
```

Особенности TRON:
- **Определения классов** выносят повторяющиеся наборы свойств в заголовок
- **Создание экземпляров** использует позиционные или именованные аргументы: `A(1,2)` или `A(x=1,y=2)`
- **Наследование**: `class B(A): z` создаёт класс B со свойствами A плюс `z`
- **Комментарии**: `#` до конца строки
- Trailing запятые разрешены в массивах, объектах и экземплярах классов
- Объекты с 1 свойством сериализуются как обычный JSON `{"key":val}`

### GOON

GOON (Greatly Optimized Object Notation) — надмножество JSON с YAML-подобной индентацией.

Вход:
```json
{"name":"John","age":30,"active":true,"items":[10,20,30]}
```

Выход:
```goon
name: John
age: 30
active: T
items[]:
  - 10
  - 20
  - 30
```

Особенности GOON:
- **Индентация-based** вложенность (2 пробела)
- **Односимвольные литералы**: `T`=true, `F`=false, `_`=null, `~`=пустая строка
- **Inline списки**: `[N]: val1,val2,val3` или `[]:` с `- value` элементами
- **Табличные массивы**: `name[N]{col1,col2}:` заголовок с разделёнными запятыми строками
- **Ссылки на словари**: `$: $1="value"` на первой строке, `$1` разрешается позже
- **Комментарии**: `#` до конца строки
- Полный round-trip: `toGOON()` затем `fromGOON()` возвращает исходные данные

### Protobuf

Protobuf (Protocol Buffers) — бинарный формат сериализации от Google. Библиотека поддерживает как бинарный wire-формат, так и читаемый текстовый формат.

```cpp
asvJSON json;
json.putString("name", "Alice");
json.putInt("age", 30);
json.putBool("active", true);

// Схемо-управляемая бинарная кодировка
std::string schema = R"({
  "name":{"id":1,"type":"string"},
  "age":{"id":2,"type":"int32"},
  "active":{"id":3,"type":"bool"}
})";

auto buf = json.toProtobuf(schema);
// buf.size() < json.serialize().size() — компактный бинарный формат

asvJSON json2;
json2.fromProtobuf(buf.data(), buf.size(), schema);

// Читаемый текстовый формат
std::string text = json.toProtobufText();
// name: "Alice"
// age: 30
// active: true
```

### TOML

TOML (Tom's Obvious Minimal Language) — формат файлов конфигурации. Библиотека поддерживает полный round-trip сериализации и парсинга.

```cpp
asvJSON json;
json.parse(R"({"name":"John","age":30,"active":true})");

// Кодирование в TOML
std::string toml = json.toTOML();
// toml:
// name = "John"
// age = 30
// active = true

// Декодирование из TOML
asvJSON j2;
j2.fromTOML(toml);
// j2.getString("name") == "John"
// j2.getInt("age") == 30
```

Особенности TOML:
- **Таблицы и вложенные таблицы**: `[table]` / `[table.subtable]` заголовки
- **Массивы таблиц**: `[[array]]` заголовки для структур массив объектов
- **Inline таблицы**: `{key = value, ...}` компактный синтаксис
- **Inline массивы**: `[1, 2, 3]` с типизированными значениями
- **Многострочные строки**: `"""..."""` basic и `'''...'''` literal
- **Числовые типы**: целые (decimal, hex `0x`, octal `0o`, binary `0b`), float, boolean
- **Точечные ключи**: `a.b.c = 42` создаёт вложенную структуру
- **Кавычкованные ключи**: `"key with spaces"` через JSON-style basic strings
- **Литеральные строки**: `'C:\Windows'` для raw неэкранированных строк
- **Комментарии**: `#` до конца строки, как полные строки так и inline
- **Обработка null**: TOML не имеет null типа; null значения пропускаются при кодировании

### JSON Lines (NDJSON)

JSON Lines (также NDJSON) — текстовый формат где каждая строка является валидным JSON значением. Идеально подходит для стриминга данных, логов и пакетных операций с базами данных.

```cpp
asvJSON json;
json.parse(R"([{"name":"Alice","age":30},{"name":"Bob","age":25}])");

// Кодирование в JSON Lines
std::string jl = json.toJSONLines();
// jl:
// {"name":"Alice","age":30}
// {"name":"Bob","age":25}

// Декодирование из JSON Lines
asvJSON j2;
j2.fromJSONLines(jl);
// j2.getRoot()->size() == 2
```

### S-Expression

S-Expression — Lisp-стиль вложенных списков с эвристическим определением объект/массив.

Вход:
```json
{"name":"John","age":30,"active":true}
```

Выход:
```sexpr
(name "John" age 30 active #t)
```

Особенности S-Expression:
- **Чётная длина группы** = объект (каждый чётный индекс — ключ), нечётная = массив
- **Комментарии**: `;` до конца строки
- **Литералы**: `nil`=null, `#t`=true, `#f`=false
- Полный round-trip: `toSexpr()` затем `fromSexpr()` возвращает исходные данные

### JSON Schema Validation

```cpp
asvJSON doc;
doc.parse(R"({"name":"Alice","age":30})");

asvJSON schema;
schema.parse(R"({
  "type": "object",
  "required": ["name"],
  "properties": {
    "name": {"type": "string"},
    "age": {"type": "integer", "minimum": 0}
  }
})");

std::string error;
bool valid = doc.validate(schema, error); // true
```

Поддерживается: `type`, `const`, `enum`, `allOf`/`anyOf`/`oneOf`/`not`, `if`/`then`/`else`, `$ref` (локальный), `minLength`/`maxLength`/`pattern`, `minimum`/`maximum`/`exclusiveMinimum`/`exclusiveMaximum`, `minItems`/`maxItems`/`uniqueItems`/`items`/`contains`, `required`/`properties`/`patternProperties`/`additionalProperties`.

Отключается через `#define ASVJSON_DISABLE_SCHEMA`.

### JSONPath (RFC 9535)

```cpp
asvJSON doc;
doc.parse(R"({"users":[{"name":"Alice","age":30},{"name":"Bob","age":25}]})");

auto results = doc.query("$.users[?(@.age >= 30)].name");
// results[0] указывает на "Alice"

const asvJSONValue* first = doc.queryFirst("$.users[0].name");
// first указывает на "Alice"

bool valid = asvJSON::isValidPath("$.users[*].name"); // true
```

Поддерживается: `$`, `.member`, `.'quoted member'`, `['member']`, `[index]`, `[start:end:step]` срезы, `..` рекурсивный спуск, `[*]` wildcard, `?(filter)` с операторами `<`, `>`, `<=`, `>=`, `==`, `!=`, `&&`, `||`, `!`, и функции: `length()`, `count()`, `min()`, `max()`, `sum()`, `avg()`, `match()`, `search()`, `value()`.

Отключается через `#define ASVJSON_DISABLE_JSONPATH`.

### JSON SAX / Потоковый парсер

```cpp
// Полноформатный SAX парсинг
struct Handler : public JsonSaxHandler {
  std::string error;
  void onKey(std::string_view key) override { ... }
  void onStringValue(std::string_view value) override { ... }
  void onStartArray() override { ... }
  void onError(std::string_view message) override { error = message; }
  // ... другие callback'и
};

asvJSON doc;
Handler h;
JsonSaxParser parser;
if (!parser.parse(jsonString, h)) {
  std::cerr << parser.getLastError();
}

// Потоковый парсинг (для больших файлов)
JsonStreamParser stream(h);
while (auto chunk = readNextChunk()) {
  if (!stream.feed(chunk)) break;
}
stream.finish();
```

Отключается через `#define ASVJSON_DISABLE_SAX`.

## Спецификации форматов

| Категория | Формат | Спецификация |
|-----------|--------|--------------|
| Промышленные стандарты | JSON | [json.org](https://www.json.org/), [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259) |
| Промышленные стандарты | BSON | [bsonspec.org](https://bsonspec.org/) |
| Промышленные стандарты | MessagePack | [msgpack.org](https://msgpack.org/) |
| Промышленные стандарты | CBOR | [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949) |
| Промышленные стандарты | XML | [W3C XML](https://www.w3.org/TR/xml/) |
| Промышленные стандарты | YAML | [YAML 1.2 spec](https://yaml.org/spec/1.2.2/) |
| Промышленные стандарты | CSV | [RFC 4180](https://www.rfc-editor.org/rfc/rfc4180) |
| Промышленные стандарты | TOML | [toml.io](https://toml.io/en/) |
| Промышленные стандарты | Protobuf | [protobuf.dev](https://protobuf.dev/) |
| Бинарные форматы | UBJSON | [ubjson.org](https://ubjson.org/) |
| Бинарные форматы | Bencode | [Wikipedia: Bencode](https://en.wikipedia.org/wiki/Bencode) |
| Бинарные форматы | Plist | [Apple NSKeyedArchiver / .plist](https://developer.apple.com/documentation/foundation/nskeyedarchiver) |
| Человеко-читаемые | JSON5 | [spec.json5.org](https://spec.json5.org/) |
| Человеко-читаемые | INI | [Wikipedia: Configuration file](https://en.wikipedia.org/wiki/Configuration_file) |
| Человеко-читаемые | Dotenv | [dotenvx.com](https://dotenvx.com/) |
| Человекочитаемые | JavaProp | [Java Properties](https://docs.oracle.com/javase/8/docs/api/java/util/Properties.html) |
| Человеко-читаемые | RON | [ron-rs/ron](https://github.com/ron-rs/ron) |
| Человеко-читаемые | S-Expression | [Wikipedia: S-expression](https://en.wikipedia.org/wiki/S-expression) |
| Человеко-читаемые | JSON Lines (NDJSON) | [ndjson.org](https://ndjson.org/) |
| Человеко-читаемые | ADE | [github.com/asvzzz/ADE](https://github.com/asvzzz/ADE) |
| Конфиг / env форматы | TOON | [github.com/toon-format/spec](https://github.com/toon-format/spec) |
| Конфиг / env форматы | TRON | [tron-format.github.io](https://tron-format.github.io/) |
| Конфиг / env форматы | GOON | [GOON-format/SPEC.md](https://github.com/GOON-format/goon/blob/main/SPEC.md) |
| Платформенные | C++ codegen | Внутренний (asvJSON++) |
| Графика / разметка | SVG | [W3C SVG](https://www.w3.org/TR/SVG2/) |

## Требования

- Компилятор C++17 (GCC 7+, Clang 5+, MSVC 2017+)
- Без внешних зависимостей
- Поддержка Windows, Linux, macOS

## Установка

Библиотека header-only — просто добавьте `asvjson/` в include path:

```bash
# Клонирование
git clone https://github.com/asvzzz/asvJSON++.git
cd asvJSON++

# Сборка тестов (опционально)
mkdir build && cd build
cmake ..
cmake --build . --config Release
./Release/test.exe
```

## Примеры

Смотрите `examples.cpp` для полных примеров использования всех форматов.

## Вклад в проект

Pull requests приветствуются! Пожалуйста, убедитесь что:
1. Все тесты проходят (`ctest`)
2. Код следует style guide проекта
3. Изменения протестированы на нескольких платформах

## Контакты

- Email: asvzzz@narod.ru
- GitHub: https://github.com/asvzzz/asvJSON++
