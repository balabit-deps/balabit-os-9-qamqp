#include <QtTest/QtTest>

#include "signalspy.h"
#include "qamqptestcase.h"

#include "qamqpclient.h"
#include "qamqpexchange.h"
#include "qamqpqueue.h"

class tst_QAMQPExchange : public TestCase
{
    Q_OBJECT
private Q_SLOTS:
    void init();
    void cleanup();

    void standardTypes_data();
    void standardTypes();
    void invalidStandardDeclaration_data();
    void invalidStandardDeclaration();
    void invalidDeclaration();
    void invalidRedeclaration();
    void removeIfUnused();
    void invalidMandatoryRouting();
    void invalidImmediateRouting();
    void confirmsSupport();
    void confirmDontLoseMessages();
    void passiveDeclareNotFound();
    void cleanupOnDeletion();
    void testQueuedPublish();
    void testRejectedMessagePublish();

private:
    QScopedPointer<QAmqpClient> client;

};

void tst_QAMQPExchange::init()
{
    client.reset(new QAmqpClient);
    client->connectToHost();
    QVERIFY(waitForSignal(client.data(), SIGNAL(connected())));
}

void tst_QAMQPExchange::cleanup()
{
    if (client->isConnected()) {
        client->disconnectFromHost();
        QVERIFY(waitForSignal(client.data(), SIGNAL(disconnected())));
    }
}

void tst_QAMQPExchange::standardTypes_data()
{
    QTest::addColumn<QAmqpExchange::ExchangeType>("type");
    QTest::addColumn<bool>("delayedDeclaration");

    QTest::newRow("direct") << QAmqpExchange::Direct << false;
    QTest::newRow("direct-delayed") << QAmqpExchange::Direct << true;
    QTest::newRow("fanout") << QAmqpExchange::FanOut << false;
    QTest::newRow("fanout-delayed") << QAmqpExchange::FanOut << true;
    QTest::newRow("topic") << QAmqpExchange::Topic << false;
    QTest::newRow("topic-delayed") << QAmqpExchange::Topic << true;
    QTest::newRow("headers") << QAmqpExchange::Headers << false;
    QTest::newRow("headers-delayed") << QAmqpExchange::Headers << true;
}

void tst_QAMQPExchange::standardTypes()
{
    QFETCH(QAmqpExchange::ExchangeType, type);
    QFETCH(bool, delayedDeclaration);

    QAmqpExchange *exchange = client->createExchange("test");
    if (!delayedDeclaration)
        QVERIFY(waitForSignal(exchange, SIGNAL(opened())));

    exchange->declare(type);
    QVERIFY(waitForSignal(exchange, SIGNAL(declared())));
    exchange->remove(QAmqpExchange::roForce);
    QVERIFY(waitForSignal(exchange, SIGNAL(removed())));
}

void tst_QAMQPExchange::invalidStandardDeclaration_data()
{
    QTest::addColumn<QString>("exchangeName");
    QTest::addColumn<QAmqpExchange::ExchangeType>("type");
    QTest::addColumn<QAMQP::Error>("error");

    QTest::newRow("amq.direct") << "amq.direct" << QAmqpExchange::Direct << QAMQP::PreconditionFailedError;
    QTest::newRow("amq.fanout") << "amq.fanout" << QAmqpExchange::FanOut << QAMQP::PreconditionFailedError;
    QTest::newRow("amq.headers") << "amq.headers" << QAmqpExchange::Headers << QAMQP::PreconditionFailedError;
    QTest::newRow("amq.match") << "amq.match" << QAmqpExchange::Headers << QAMQP::PreconditionFailedError;
    QTest::newRow("amq.topic") << "amq.topic" << QAmqpExchange::Topic << QAMQP::PreconditionFailedError;
    QTest::newRow("amq.reserved") << "amq.reserved" << QAmqpExchange::Direct << QAMQP::AccessRefusedError;
}

void tst_QAMQPExchange::invalidStandardDeclaration()
{
    QFETCH(QString, exchangeName);
    QFETCH(QAmqpExchange::ExchangeType, type);
    QFETCH(QAMQP::Error, error);

    QAmqpExchange *exchange = client->createExchange(exchangeName);
    exchange->declare(type);
    QVERIFY(waitForSignal(exchange, SIGNAL(error(QAMQP::Error))));
    QCOMPARE(exchange->error(), error);
}

void tst_QAMQPExchange::invalidDeclaration()
{
    QAmqpExchange *exchange = client->createExchange("test-invalid-declaration");
    exchange->declare("invalidExchangeType");
    QVERIFY(waitForSignal(client.data(), SIGNAL(error(QAMQP::Error))));
    QCOMPARE(client->error(), QAMQP::CommandInvalidError);
}

void tst_QAMQPExchange::invalidRedeclaration()
{
    QAmqpExchange *exchange = client->createExchange("test-invalid-redeclaration");
    exchange->declare(QAmqpExchange::Direct);
    QVERIFY(waitForSignal(exchange, SIGNAL(declared())));

    QAmqpExchange *redeclared = client->createExchange("test-invalid-redeclaration");
    redeclared->declare(QAmqpExchange::FanOut);
    QVERIFY(waitForSignal(redeclared, SIGNAL(error(QAMQP::Error))));

    // this is per spec:
    // QCOMPARE(redeclared->error(), QAMQP::NotAllowedError);

    // this is for rabbitmq:
    QCOMPARE(redeclared->error(), QAMQP::PreconditionFailedError);

    // Server has probably closed the channel on us, if so, re-open it.
    if (!exchange->isOpen()) {
        exchange->reopen();
        QVERIFY(waitForSignal(exchange, SIGNAL(opened())));
    }

    // cleanup
    exchange->remove();
    QVERIFY(waitForSignal(exchange, SIGNAL(removed())));
}

void tst_QAMQPExchange::removeIfUnused()
{
    QAmqpExchange *exchange = client->createExchange("test-if-unused-exchange");
    exchange->declare(QAmqpExchange::Direct, QAmqpExchange::AutoDelete);
    QVERIFY(waitForSignal(exchange, SIGNAL(declared())));

    QAmqpQueue *queue = client->createQueue("test-if-unused-queue");
    queue->declare();
    QVERIFY(waitForSignal(queue, SIGNAL(declared())));
    queue->bind("test-if-unused-exchange", "testRoutingKey");
    QVERIFY(waitForSignal(queue, SIGNAL(bound())));

    exchange->remove(QAmqpExchange::roIfUnused);
    QVERIFY(waitForSignal(exchange, SIGNAL(error(QAMQP::Error))));
    QCOMPARE(exchange->error(), QAMQP::PreconditionFailedError);
    QVERIFY(!exchange->errorString().isEmpty());

    // cleanup
    queue->remove(QAmqpQueue::roForce);
    QVERIFY(waitForSignal(queue, SIGNAL(removed())));
}

void tst_QAMQPExchange::invalidMandatoryRouting()
{
    QAmqpExchange *defaultExchange = client->createExchange();
    defaultExchange->publish("some message", "unroutable-key", QAmqpMessage::PropertyHash(), QAmqpExchange::poMandatory);
    QVERIFY(waitForSignal(defaultExchange, SIGNAL(error(QAMQP::Error))));
    QCOMPARE(defaultExchange->error(), QAMQP::NoRouteError);
}

void tst_QAMQPExchange::invalidImmediateRouting()
{
    QAmqpExchange *defaultExchange = client->createExchange();
    defaultExchange->publish("some message", "unroutable-key", QAmqpMessage::PropertyHash(), QAmqpExchange::poImmediate);
    QVERIFY(waitForSignal(client.data(), SIGNAL(error(QAMQP::Error))));
    QCOMPARE(client->error(), QAMQP::NotImplementedError);
}

void tst_QAMQPExchange::confirmsSupport()
{
    QAmqpExchange *exchange = client->createExchange("confirm-test");
    exchange->enableConfirms();
    QVERIFY(waitForSignal(exchange, SIGNAL(confirmsEnabled())));
}

void tst_QAMQPExchange::confirmDontLoseMessages()
{
    QAmqpExchange *defaultExchange = client->createExchange();
    defaultExchange->enableConfirms();
    QVERIFY(waitForSignal(defaultExchange, SIGNAL(confirmsEnabled())));

    QAmqpMessage::PropertyHash properties;
    properties[QAmqpMessage::DeliveryMode] = "2";   // make message persistent

    for (int i = 0; i < 10000; ++i)
        defaultExchange->publish("noop", "confirms-test", properties);
    QVERIFY(defaultExchange->waitForConfirms());
}

void tst_QAMQPExchange::passiveDeclareNotFound()
{
    QAmqpExchange *nonExistentExchange = client->createExchange("this-does-not-exist");
    nonExistentExchange->declare(QAmqpExchange::Direct, QAmqpExchange::Passive);
    QVERIFY(waitForSignal(nonExistentExchange, SIGNAL(error(QAMQP::Error))));
    QCOMPARE(nonExistentExchange->error(), QAMQP::NotFoundError);
}

void tst_QAMQPExchange::cleanupOnDeletion()
{
    // create, declare, and close the wrong way
    QAmqpExchange *exchange = client->createExchange("test-deletion");
    exchange->declare();
    QVERIFY(waitForSignal(exchange, SIGNAL(declared())));
    exchange->close();
    exchange->deleteLater();
    QVERIFY(waitForSignal(exchange, SIGNAL(destroyed())));

    // now create, declare, and close the right way
    exchange = client->createExchange("test-deletion");
    exchange->declare();
    QVERIFY(waitForSignal(exchange, SIGNAL(declared())));
    exchange->close();
    QVERIFY(waitForSignal(exchange, SIGNAL(closed())));
}

void tst_QAMQPExchange::testQueuedPublish()
{
    QAmqpExchange *defaultExchange = client->createExchange();
    defaultExchange->enableConfirms();
    QVERIFY(waitForSignal(defaultExchange, SIGNAL(confirmsEnabled())));

    SignalSpy deliveryConfirmedSpy(defaultExchange, SIGNAL(deliveryConfirmed(qlonglong)));
    SignalSpy allMessagesDeliveredSpy(defaultExchange, SIGNAL(allMessagesDelivered()));
    SignalSpy messageDeliveryFinishedSpy(defaultExchange, SIGNAL(messageDeliveryFinished(QVector<qlonglong>)));

    QAmqpMessage::PropertyHash properties;
    properties[QAmqpMessage::DeliveryMode] = "2";   // make message persistent
    for (int i = 0; i < 10000; ++i) {
        QMetaObject::invokeMethod(defaultExchange, "publish", Qt::QueuedConnection,
                                  Q_ARG(QString, "noop"), Q_ARG(QString, "confirms-test"),
                                  Q_ARG(QAmqpMessage::PropertyHash, properties));
    }

    QVERIFY(defaultExchange->waitForConfirms());

    QCOMPARE(allMessagesDeliveredSpy.count(), 1);

    QCOMPARE(messageDeliveryFinishedSpy.count(), 1);
    QVector< qlonglong > rejectedDeliveryTags = messageDeliveryFinishedSpy.first().first().value< QVector< qlonglong > >();
    QVERIFY(rejectedDeliveryTags.isEmpty());

    QCOMPARE(deliveryConfirmedSpy.count(), 10000);
    for (int i = 0; i < 10000; ++i) {
        qlonglong deliveryTag = deliveryConfirmedSpy.at(i).first().toLongLong();
        QCOMPARE(deliveryTag, i+1);
      }
}

void tst_QAMQPExchange::testRejectedMessagePublish()
{
    QAmqpTable queueArguments;
    queueArguments.insert("x-max-length", 1);  // queue can only accept one message!
    queueArguments.insert("x-overflow", "reject-publish");  // more messages will be rejected
    QAmqpQueue *queue = client->createQueue("small_queue");
    queue->declare(QAmqpQueue::Exclusive | QAmqpQueue::AutoDelete, queueArguments);
    QVERIFY(waitForSignal(queue, SIGNAL(declared())));

    QAmqpExchange *exchange = client->createExchange();
    exchange->enableConfirms();
    QVERIFY(waitForSignal(exchange, SIGNAL(confirmsEnabled())));

    SignalSpy deliveryConfirmedSpy(exchange, SIGNAL(deliveryConfirmed(qlonglong)));
    SignalSpy deliveryRejectedSpy(exchange, SIGNAL(deliveryRejected(qlonglong)));
    SignalSpy allMessagesDeliveredSpy(exchange, SIGNAL(allMessagesDelivered()));
    SignalSpy messageDeliveryFinishedSpy(exchange, SIGNAL(messageDeliveryFinished(QVector<qlonglong>)));

    qlonglong firstDeliveryTag = exchange->publish("message", "small_queue");
    qlonglong secondDeliveryTag = exchange->publish("message", "small_queue");
    QVERIFY(firstDeliveryTag != secondDeliveryTag);

    QVERIFY(exchange->waitForConfirms());

    if (deliveryConfirmedSpy.count() == 2)
      {
        QSKIP("The AMQP server does not support x-overflow: reject-publish. For rabbitmq, it requires version >=3.7");
      }

    QCOMPARE(deliveryConfirmedSpy.count(), 1);
    QCOMPARE(deliveryConfirmedSpy.first().first().toLongLong(), firstDeliveryTag);

    QCOMPARE(deliveryRejectedSpy.count(), 1);
    QCOMPARE(deliveryRejectedSpy.first().first().toLongLong(), secondDeliveryTag);

    QCOMPARE(allMessagesDeliveredSpy.count(), 0); // not all messages were delivered

    QCOMPARE(messageDeliveryFinishedSpy.count(), 1);
    QVector< qlonglong > rejectedDeliveryTags = messageDeliveryFinishedSpy.first().first().value< QVector< qlonglong > >();
    QCOMPARE(rejectedDeliveryTags.count(), 1);
    QCOMPARE(rejectedDeliveryTags.first(), secondDeliveryTag);
}

QTEST_MAIN(tst_QAMQPExchange)
#include "tst_qamqpexchange.moc"
