#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>

static QString jsonValueToString(const QJsonValue& value, int indent)
{
    const QString pad = QString(indent * 2, ' ');
    const QString padInner = QString((indent + 1) * 2, ' ');

    switch (value.type()) {
    case QJsonValue::Null:   return "null";
    case QJsonValue::Bool:   return value.toBool() ? "true" : "false";
    case QJsonValue::Double: return QString::number(value.toDouble());
    case QJsonValue::String: return "\"" + value.toString() + "\"";

    case QJsonValue::Array: {
        const QJsonArray arr = value.toArray();
        if (arr.isEmpty()) return "[]";
        QString s = "[\n";
        for (int i = 0; i < arr.size(); ++i) {
            s += padInner + jsonValueToString(arr[i], indent + 1);
            if (i < arr.size() - 1) s += ",";
            s += "\n";
        }
        s += pad + "]";
        return s;
    }

    case QJsonValue::Object: {
        const QJsonObject obj = value.toObject();
        if (obj.isEmpty()) return "{}";
        QString s = "{\n";
        int i = 0;
        for (auto it = obj.begin(); it != obj.end(); ++it, ++i) {
            s += padInner + "\"" + it.key() + "\": "
               + jsonValueToString(it.value(), indent + 1);
            if (i < obj.size() - 1) s += ",";
            s += "\n";
        }
        s += pad + "}";
        return s;
    }

    default:
        return "<unknown>";
    }
}

void printJsonObject(const QJsonObject& obj, const QString& title = QString())
{
    if (!title.isEmpty())
        qDebug().noquote() << "=== " << title << " ===";
    qDebug().noquote() << jsonValueToString(QJsonValue(obj), 0);
}
