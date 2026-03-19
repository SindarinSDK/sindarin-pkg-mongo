# sindarin-pkg-mongo

A MongoDB client for the [Sindarin](https://github.com/SindarinSDK/sindarin-compiler) programming language, backed by [libmongoc](https://mongoc.org/). Supports inserting, querying, updating, and deleting documents using JSON strings, with typed field accessors on result documents.

## Installation

Add the package as a dependency in your `sn.yaml`:

```yaml
dependencies:
- name: sindarin-pkg-mongo
  git: git@github.com:SindarinSDK/sindarin-pkg-mongo.git
  branch: main
```

Then run `sn --install` to fetch the package.

## Quick Start

```sindarin
import "sindarin-pkg-mongo/src/mongo"

fn main(): void =>
    var client: MongoClient = MongoClient.connect("mongodb://localhost:27017")
    var coll: MongoCollection = client.collection("mydb", "users")

    coll.insertOne("{\"name\": \"Alice\", \"age\": 30}")

    var docs: MongoDoc[] = coll.find("{\"name\": \"Alice\"}")
    print(docs[0].getString("name"))
    print(docs[0].getInt("age"))

    coll.dispose()
    client.dispose()
```

---

## MongoClient

```sindarin
import "sindarin-pkg-mongo/src/mongo"
```

A connection to a MongoDB server. The URI follows the [standard MongoDB connection string format](https://www.mongodb.com/docs/manual/reference/connection-string/).

| Method | Signature | Description |
|--------|-----------|-------------|
| `connect` | `static fn connect(uri: str): MongoClient` | Connect to a MongoDB server |
| `collection` | `fn collection(db: str, name: str): MongoCollection` | Get a named collection from a database |
| `dispose` | `fn dispose(): void` | Close the connection |

```sindarin
var client: MongoClient = MongoClient.connect("mongodb://localhost:27017")

# with authentication
var client: MongoClient = MongoClient.connect("mongodb://user:pass@host:27017/mydb")

var coll: MongoCollection = client.collection("mydb", "users")
coll.dispose()
client.dispose()
```

---

## MongoCollection

A reference to a collection within a database. All filter and update arguments are JSON strings.

| Method | Signature | Description |
|--------|-----------|-------------|
| `insertOne` | `fn insertOne(json: str): void` | Insert a document given as a JSON string |
| `find` | `fn find(filter: str): MongoDoc[]` | Query with a JSON filter, return all matches |
| `updateOne` | `fn updateOne(filter: str, update: str): void` | Update the first matching document |
| `updateMany` | `fn updateMany(filter: str, update: str): void` | Update all matching documents |
| `deleteOne` | `fn deleteOne(filter: str): void` | Delete the first matching document |
| `deleteMany` | `fn deleteMany(filter: str): void` | Delete all matching documents |
| `count` | `fn count(filter: str): int` | Count documents matching the filter |
| `dispose` | `fn dispose(): void` | Free collection resources |

```sindarin
coll.insertOne("{\"name\": \"Bob\", \"age\": 25, \"active\": true}")

var docs: MongoDoc[] = coll.find("{\"active\": true}")
var n: int = coll.count("{}")

coll.updateOne("{\"name\": \"Bob\"}", "{\"$set\": {\"age\": 26}}")
coll.deleteOne("{\"name\": \"Bob\"}")
```

---

## MongoDoc

A single MongoDB document returned from a query. Field values are accessed by name using typed getters. The underlying BSON data is copied at query time so the document is safe to use after the query returns.

| Method | Signature | Description |
|--------|-----------|-------------|
| `getString` | `fn getString(key: str): str` | Field value as string (`""` if missing or null) |
| `getInt` | `fn getInt(key: str): int` | Field value as integer (`0` if missing or null) |
| `getFloat` | `fn getFloat(key: str): double` | Field value as float (`0.0` if missing or null) |
| `getBool` | `fn getBool(key: str): bool` | Field value as boolean (`false` if missing or null) |
| `isNull` | `fn isNull(key: str): bool` | True if the field is missing or BSON null |
| `toJson` | `fn toJson(): str` | Document as a relaxed extended JSON string |

```sindarin
var docs: MongoDoc[] = coll.find("{\"name\": \"Alice\"}")

print(docs[0].getString("name"))   # "Alice"
print(docs[0].getInt("age"))       # 30
print(docs[0].getFloat("score"))   # 9.5
print(docs[0].getBool("active"))   # true

if docs[0].isNull("notes") =>
    print("no notes\n")

print(docs[0].toJson())
```

---

## Examples

### Basic CRUD

```sindarin
import "sindarin-pkg-mongo/src/mongo"

fn main(): void =>
    var client: MongoClient = MongoClient.connect("mongodb://localhost:27017")
    var coll: MongoCollection = client.collection("mydb", "products")

    coll.insertOne("{\"name\": \"alpha\", \"stock\": 10}")
    coll.insertOne("{\"name\": \"beta\",  \"stock\": 5}")

    var docs: MongoDoc[] = coll.find("{}")
    for i: int = 0; i < docs.length; i += 1 =>
        print($"{docs[i].getString(\"name\")}: {docs[i].getInt(\"stock\")}\n")

    coll.updateOne("{\"name\": \"beta\"}", "{\"$set\": {\"stock\": 0}}")
    coll.deleteMany("{\"stock\": 0}")

    coll.dispose()
    client.dispose()
```

### Filtering and counting

```sindarin
import "sindarin-pkg-mongo/src/mongo"

fn main(): void =>
    var client: MongoClient = MongoClient.connect("mongodb://localhost:27017")
    var coll: MongoCollection = client.collection("mydb", "events")

    var active: MongoDoc[] = coll.find("{\"status\": \"active\"}")
    var total: int = coll.count("{}")
    var activeCount: int = coll.count("{\"status\": \"active\"}")

    print($"active: {activeCount} / {total}\n")

    coll.dispose()
    client.dispose()
```

### Bulk update

```sindarin
import "sindarin-pkg-mongo/src/mongo"

fn main(): void =>
    var client: MongoClient = MongoClient.connect("mongodb://localhost:27017")
    var coll: MongoCollection = client.collection("mydb", "users")

    # Mark all users as needing a password reset
    coll.updateMany("{}", "{\"$set\": {\"resetRequired\": true}}")

    coll.dispose()
    client.dispose()
```

---

## Development

```bash
# Install dependencies (required before make test)
sn --install

make test    # Build and run all tests
make clean   # Remove build artifacts
```

Tests require a running MongoDB server. Set the following environment variable before running:

| Variable | Default | Description |
|----------|---------|-------------|
| `MONGODB_URI` | `mongodb://localhost:27017` | MongoDB connection URI |

## Dependencies

- [sindarin-pkg-libs](https://github.com/SindarinSDK/sindarin-pkg-libs) — provides pre-built `libmongoc2` and `libbson2` static libraries for Linux, macOS, and Windows.
- [sindarin-pkg-sdk](https://github.com/SindarinSDK/sindarin-pkg-sdk) — Sindarin standard library.

## License

MIT License
