#include "chatserver.h"
#include "serverworker.h"
#include <QThread>
#include <functional>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QtXml>
#include <QTextStream>
#include <QRegularExpression>
ChatServer::ChatServer(QObject *parent)
    : QTcpServer(parent)
    , m_idealThreadCount(qMax(QThread::idealThreadCount(), 1))
{
    m_availableThreads.reserve(m_idealThreadCount);
    m_threadsLoad.reserve(m_idealThreadCount);
}

ChatServer::~ChatServer()
{
    for (QThread *singleThread : m_availableThreads) {
        singleThread->quit();
        singleThread->wait();
    }
}
void ChatServer::incomingConnection(qintptr socketDescriptor)
{
    ServerWorker *worker = new ServerWorker;
    if (!worker->setSocketDescriptor(socketDescriptor)) {
        worker->deleteLater();
        return;
    }
    int threadIdx = m_availableThreads.size();
    if (threadIdx < m_idealThreadCount) { //we can add a new thread
        m_availableThreads.append(new QThread(this));
        m_threadsLoad.append(1);
        m_availableThreads.last()->start();
    } else {
        // find the thread with the least amount of clients and use it
        threadIdx = std::distance(m_threadsLoad.cbegin(), std::min_element(m_threadsLoad.cbegin(), m_threadsLoad.cend()));
        ++m_threadsLoad[threadIdx];
    }
    worker->moveToThread(m_availableThreads.at(threadIdx));
    connect(m_availableThreads.at(threadIdx), &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &ServerWorker::disconnectedFromClient, this, std::bind(&ChatServer::userDisconnected, this, worker, threadIdx));
    connect(worker, &ServerWorker::error, this, std::bind(&ChatServer::userError, this, worker));
    connect(worker, &ServerWorker::jsonReceived, this, std::bind(&ChatServer::jsonReceived, this, worker, std::placeholders::_1));
    connect(this, &ChatServer::stopAllClients, worker, &ServerWorker::disconnectFromClient);
    m_clients.append(worker);
    emit logMessage(QStringLiteral("New client Connected"));

}
void ChatServer::sendJson(ServerWorker *destination, const QJsonObject &message)
{
    Q_ASSERT(destination);
    QTimer::singleShot(0, destination, std::bind(&ServerWorker::sendJson, destination, message));
}
void ChatServer::broadcast(const QJsonObject &message, ServerWorker *exclude)
{
    QJsonValue value = message.value(QString("destination"));

    for (ServerWorker *worker : m_clients) {
        Q_ASSERT(worker);
        if (worker->userName() != value.toString())//used to be worker == exclude skip but if worker != destination skip
            continue;
        sendJson(worker, message);
    }
}

void ChatServer::jsonReceived(ServerWorker *sender, const QJsonObject &json)
{
    Q_ASSERT(sender);
    emit logMessage(QLatin1String("JSON received ") + QString::fromUtf8(QJsonDocument(json).toJson()));
    if (sender->userName().isEmpty())
        return jsonFromLoggedOut(sender, json);
    //used to be sender but lets try the exclude
    QJsonValue value = json.value(QString("destination"));
    qDebug() << value.toString();
    jsonFromLoggedIn(sender, json);

}

void ChatServer::userDisconnected(ServerWorker *sender, int threadIdx)
{
    --m_threadsLoad[threadIdx];
    m_clients.removeAll(sender);
    const QString userName = sender->userName();
    if (!userName.isEmpty()) {
        QJsonObject disconnectedMessage;
        disconnectedMessage[QStringLiteral("type")] = QStringLiteral("userdisconnected");
        disconnectedMessage[QStringLiteral("username")] = userName;
        broadcast(disconnectedMessage, nullptr);
        emit logMessage(userName + QLatin1String(" disconnected"));
    }
    sender->deleteLater();
}

void ChatServer::userError(ServerWorker *sender)
{
    Q_UNUSED(sender)
    emit logMessage(QLatin1String("Error from ") + sender->userName());
}

void ChatServer::stopServer()
{
    emit stopAllClients();
    close();
}

void ChatServer::jsonFromLoggedOut(ServerWorker *sender, const QJsonObject &docObj)
{
    Q_ASSERT(sender);
    const QJsonValue typeVal = docObj.value(QLatin1String("type"));
    if (typeVal.isNull() || !typeVal.isString())
        return;
    if (typeVal.toString().compare(QLatin1String("login"), Qt::CaseInsensitive) != 0)
        return;
    const QJsonValue usernameVal = docObj.value(QLatin1String("username"));
    if (usernameVal.isNull() || !usernameVal.isString())
        return;
    const QString newUserName = usernameVal.toString().simplified();
    if (newUserName.isEmpty())
        return;
    for (ServerWorker *worker : qAsConst(m_clients)) {
        if (worker == sender)
            continue;
        if (worker->userName().compare(newUserName, Qt::CaseInsensitive) == 0) {
            QJsonObject message;
            message[QStringLiteral("type")] = QStringLiteral("login");
            message[QStringLiteral("success")] = false;
            message[QStringLiteral("reason")] = QStringLiteral("duplicate username");
            sendJson(sender, message);
            return;
        }
    }
    sender->setUserName(newUserName);
    QJsonObject successMessage;
    successMessage[QStringLiteral("type")] = QStringLiteral("login");
    successMessage[QStringLiteral("success")] = true;
    sendJson(sender, successMessage);
    QJsonObject connectedMessage;
    connectedMessage[QStringLiteral("type")] = QStringLiteral("newuser");
    connectedMessage[QStringLiteral("username")] = newUserName;
    broadcast(connectedMessage, sender);

    qDebug() << "User Logged in checking stored messages" + sender->userName();


    //Check if any messages for this user exist
    QFile file("C:/Users/aniya/Documents/ChatExampleTest/QtSimpleChatServerThreaded/serverJson.xml");
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        //Failed to open
        file.close();

    }
    QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();

    qDebug() << "New worker username: " + sender->userName();
    QJsonObject RootObject = document.object();
    QJsonArray messageArray = RootObject[sender->userName()].toArray();
    qDebug() << messageArray.isEmpty();//IF EMPTY WE NEED TO RESET CHAT BEFORE OVERWRITING SO GET ROWS FROM CHAT VIEW

    for(int i(0); i < messageArray.size(); i++){

        QJsonObject msgSent = messageArray[i].toObject();
        QJsonObject sent = msgSent.value(QString("SENT")).toObject();
        QJsonValue value = sent.value(QString("text"));
        QJsonValue source = sent.value(QString("source"));
        QJsonValue destination = sent.value(QString("destination"));
        qDebug() << value.toString();

        QJsonObject message;
        message[QStringLiteral("type")] = QStringLiteral("message");
        message[QStringLiteral("text")] = value.toString();
        message[QStringLiteral("sender")] = source.toString();
        message[QStringLiteral("source")] = source.toString();
        message[QStringLiteral("destination")] = destination.toString();

        ServerWorker sender;
        sender.setUserName(source.toString());
        broadcast(message, &sender);
    }
    for(int i(0); i < messageArray.size(); i++){
        messageArray.removeLast();
    }
    messageArray.removeFirst();

    RootObject[sender->userName()] = messageArray;
    //RootObject["Messages"] = messageArray;
    document.setObject(RootObject);
    QByteArray bytes = document.toJson( QJsonDocument::Indented );

    if( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    {
        QTextStream iStream( &file );
        iStream << bytes;
        file.close();
    }

}

void ChatServer::jsonFromLoggedIn(ServerWorker *sender, const QJsonObject &docObj)
{
    Q_ASSERT(sender);
    const QJsonValue typeVal = docObj.value(QLatin1String("type"));
    if (typeVal.isNull() || !typeVal.isString())
        return;
    if (typeVal.toString().compare(QLatin1String("message"), Qt::CaseInsensitive) != 0)
        return;
    const QJsonValue textVal = docObj.value(QLatin1String("text"));
    if (textVal.isNull() || !textVal.isString())
        return;
    const QString text = textVal.toString().trimmed();
    if (text.isEmpty())
        return;
    QJsonObject message;
    message[QStringLiteral("type")] = QStringLiteral("message");
    message[QStringLiteral("text")] = text;
    message[QStringLiteral("sender")] = sender->userName();
    message[QStringLiteral("source")] = docObj.value(QLatin1String("source"));
    message[QStringLiteral("destination")] = docObj.value(QLatin1String("destination"));

    //Parse through the workers to see if dest is logged in
    QJsonValue value = docObj.value(QString("destination"));
    qDebug() << "Destination: " + value.toString();
    bool destinationActive = false;
    for (ServerWorker *worker : m_clients) {

        qDebug() << "Active workers: " + worker->userName();

        if(worker->userName() == value.toString()){
            qDebug() << "Destination is active: ";
            destinationActive = true;
        }
    }

    if(!destinationActive){
        //Need to add this message to the server JSON file for later use when the user logs in
        QFile file("C:/Users/aniya/Documents/ChatExampleTest/QtSimpleChatServerThreaded/serverJson.xml");
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text)){
            qDebug() << "Failed to open serverJson";
            file.close();

        }
        QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        file.close();

        QJsonObject RootObject = document.object();
        QJsonArray messageArray = RootObject[value.toString()].toArray();
        QJsonObject send;
        send.insert("SENT", docObj);
        messageArray.append(send);
        RootObject[value.toString()] = messageArray;
        //RootObject["Messages"] = messageArray;
        document.setObject(RootObject);
        QByteArray bytes = document.toJson( QJsonDocument::Indented );

        if( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
        {
            QTextStream iStream( &file );
            iStream << bytes;
            file.close();
        }
        else
        {
            qDebug() << "File open failed open";
        }
    }else{
        broadcast(message, sender);
    }

    //After we read we need to remove them
}


