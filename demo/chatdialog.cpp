#include <QtGui>
#include "chatdialog.h"
#include "settingdialog.h"
#include <ctime>
#include <iostream>
#include <QTimer>
#include <QMetaType>

#define BROADCAST_PREFIX_FOR_SYNC_DEMO "/ndn/broadcast/sync-demo"

void
ChatDialog::testDraw()
{
  std::string prefix[5] = {"/ndn/1", "/ndn/2", "/ndn/3", "/ndn/4", "/ndn/5"};
  std::string nick[5] = {"tom", "jerry", "jason", "michael", "hurry"};
  std::vector<Sync::MissingDataInfo> v;
  for (int i = 0; i < 5; i++)
  {
    Sync::MissingDataInfo mdi = {prefix[i], Sync::SeqNo(0), Sync::SeqNo(i * (2 << i) )};
    v.push_back(mdi);
  }

  m_scene->processUpdate(v, "12341234@!#%!@");

  for (int i = 0; i < 5; i++)
  {
   m_scene-> msgReceived(prefix[i].c_str(), nick[i].c_str());
  }

  fitView();
}

ChatDialog::ChatDialog(QWidget *parent)
  : QDialog(parent), m_sock(NULL)
{
  // have to register this, otherwise
  // the signal-slot system won't recognize this type
  qRegisterMetaType<SyncDemo::ChatMessage>("SyncDemo::ChatMessage");
  setupUi(this);
  m_session = time(NULL);

  readSettings();

  updateLabels();

  lineEdit->setFocusPolicy(Qt::StrongFocus);
  m_scene = new DigestTreeScene(this);

  treeViewer->setScene(m_scene);
  m_scene->plot("Empty");
  QRectF rect = m_scene->itemsBoundingRect();
  m_scene->setSceneRect(rect);

  // create sync socket
  if(!m_user.getChatroom().isEmpty()) {
    std::string syncPrefix = BROADCAST_PREFIX_FOR_SYNC_DEMO;
    syncPrefix += "/";
    syncPrefix += m_user.getChatroom().toStdString();
    m_sock = new Sync::SyncAppSocket(syncPrefix, bind(&ChatDialog::processTreeUpdate, this, _1, _2), bind(&ChatDialog::processRemove, this, _1));
  }

  connect(lineEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
  connect(setButton, SIGNAL(pressed()), this, SLOT(buttonPressed()));
  connect(this, SIGNAL(msgReceived(const SyncDemo::ChatMessage)), this, SLOT(appendMessage(const SyncDemo::ChatMessage)));
  //testDraw();
}

ChatDialog::~ChatDialog()
{
  if (m_sock != NULL) 
  {
    delete m_sock;
    m_sock = NULL;
  }
}

void
ChatDialog::appendMessage(const SyncDemo::ChatMessage msg) 
{

  if (msg.type() != SyncDemo::ChatMessage::CHAT) {
    return;
  }

  if (!msg.has_data()) {
    return;
  }

  if (msg.from().empty() || msg.data().empty()) {
    return;
  }

  std::cout << "<<<< Received Message: " << msg.data() << std::endl;
  QTextCursor cursor(textEdit->textCursor());
  cursor.movePosition(QTextCursor::End);
  QTextTableFormat tableFormat;
  tableFormat.setBorder(0);
  QTextTable *table = cursor.insertTable(1, 2, tableFormat);
  QString from = QString("<%1>: ").arg(msg.from().c_str());
  table->cellAt(0, 0).firstCursorPosition().insertText(from);
  table->cellAt(0, 1).firstCursorPosition().insertText(msg.data().c_str());
  QScrollBar *bar = textEdit->verticalScrollBar();
  bar->setValue(bar->maximum());
  std::cout << "<<<<, Message appended " << std::endl;
}

void
ChatDialog::processTreeUpdate(const std::vector<Sync::MissingDataInfo> &v, Sync::SyncAppSocket *)
{
  if (v.empty())
  {
    return;
  }

  // reflect the changes on digest tree
  m_scene->processUpdate(v, m_sock->getRootDigest().c_str());

  int n = v.size();
  int totalMissingPackets = 0;
  for (int i = 0; i < n; i++) 
  {
    totalMissingPackets += v[i].high.getSeq() - v[i].low.getSeq() + 1;
  }
  
  if (totalMissingPackets < 10) {
    for (int i = 0; i < n; i++) 
    {
      for (Sync::SeqNo seq = v[i].low; seq <= v[i].high; ++seq)
      {
        m_sock->fetchRaw(v[i].prefix, seq, bind(&ChatDialog::processData, this, _1, _2, _3), 2);
      }
    }
  }
  else
  {
    // too bad; too many missing packets
    // we may just join a new chatroom
    // or some network patition just healed
    // we don't try to fetch any data in this case (for now)
  }

  // adjust the view
  fitView();

}

void
ChatDialog::processData(std::string name, const char *buf, size_t len)
{
  SyncDemo::ChatMessage msg;
  if (!msg.ParseFromArray(buf, len)) 
  {
    std::cerr << "Errrrr.. Can not parse msg at "<<__FILE__ <<":"<<__LINE__<<". what is happening?" << std::endl;
    abort();
  }

  // display msg received from network
  // we have to do so; this function is called by ccnd thread
  // so if we call appendMsg directly
  // Qt crash as "QObject: Cannot create children for a parent that is in a different thread"
  // the "cannonical" way to is use signal-slot
  emit msgReceived(msg);
  
  // update the tree view
  std::string prefix = name.substr(0, name.find_last_of('/'));
  m_scene->msgReceived(prefix.c_str(), msg.from().c_str());
}

void
ChatDialog::processRemove(std::string prefix)
{
}

void
ChatDialog::formChatMessage(const QString &text, SyncDemo::ChatMessage &msg) {
  msg.set_from(m_user.getNick().toStdString());
  msg.set_to(m_user.getChatroom().toStdString());
  msg.set_data(text.toStdString());
  time_t seconds = time(NULL);
  msg.set_timestamp(seconds);
  msg.set_type(SyncDemo::ChatMessage::CHAT);
}

bool
ChatDialog::readSettings()
{
  QSettings s(ORGANIZATION, APPLICATION);
  QString nick = s.value("nick", "").toString();
  QString chatroom = s.value("chatroom", "").toString();
  QString prefix = s.value("prefix", "").toString();
  if (nick == "" || chatroom == "" || prefix == "") {
    QTimer::singleShot(500, this, SLOT(buttonPressed()));
    return false;
  }
  else {
    m_user.setNick(nick);
    m_user.setChatroom(chatroom);
    m_user.setPrefix(prefix);
    return true;
  }
}

void 
ChatDialog::writeSettings()
{
  QSettings s(ORGANIZATION, APPLICATION);
  s.setValue("nick", m_user.getNick());
  s.setValue("chatroom", m_user.getChatroom());
  s.setValue("prefix", m_user.getPrefix());
}

void
ChatDialog::updateLabels()
{
  QString settingDisp = QString("<User: %1>, <Chatroom: %2>").arg(m_user.getNick()).arg(m_user.getChatroom());
  infoLabel->setText(settingDisp);
  QString prefixDisp = QString("<Prefix: %1>").arg(m_user.getPrefix());
  prefixLabel->setText(prefixDisp);
}

void 
ChatDialog::returnPressed()
{
  QString text = lineEdit->text();
  if (text.isEmpty())
    return;
 
  lineEdit->clear();

  SyncDemo::ChatMessage msg;
  formChatMessage(text, msg);
  
  appendMessage(msg);

  // send msg
  size_t size = msg.ByteSize();
  char *buf = new char[size];
  msg.SerializeToArray(buf, size);
  if (!msg.IsInitialized()) 
  {
    std::cerr << "Errrrr.. msg was not probally initialized "<<__FILE__ <<":"<<__LINE__<<". what is happening?" << std::endl;
    abort();
  }
  m_sock->publishRaw(m_user.getPrefix().toStdString(), m_session, buf, size, 60);
  
}

void
ChatDialog::buttonPressed()
{
  SettingDialog dialog(this, m_user.getNick(), m_user.getChatroom(), m_user.getPrefix());
  connect(&dialog, SIGNAL(updated(QString, QString, QString)), this, SLOT(settingUpdated(QString, QString, QString)));
  dialog.exec();
}

void
ChatDialog::settingUpdated(QString nick, QString chatroom, QString prefix)
{
  bool needWrite = false;
  if (!nick.isEmpty() && nick != m_user.getNick()) {
    m_user.setNick(nick);
    needWrite = true;
  }
  if (!prefix.isEmpty() && prefix != m_user.getPrefix()) {
    m_user.setPrefix(prefix);
    needWrite = true;
    // TODO: set the previous prefix as left?
  }
  if (!chatroom.isEmpty() && chatroom != m_user.getChatroom()) {
    m_user.setChatroom(chatroom);
    needWrite = true;

    m_scene->clearAll();
    m_scene->plot("Empty");
    // TODO: perhaps need to do a lot. e.g. use a new SyncAppSokcet
    if (m_sock != NULL) 
    {
      delete m_sock;
      m_sock = NULL;
    }
    std::string syncPrefix = BROADCAST_PREFIX_FOR_SYNC_DEMO;
    syncPrefix += "/";
    syncPrefix += m_user.getChatroom().toStdString();
    m_sock = new Sync::SyncAppSocket(syncPrefix, bind(&ChatDialog::processTreeUpdate, this, _1, _2), bind(&ChatDialog::processRemove, this, _1));

    fitView();
    
  }
  if (needWrite) {
    writeSettings();
    updateLabels();
  }
}

void 
ChatDialog::resizeEvent(QResizeEvent *e)
{
  fitView();
}

void 
ChatDialog::showEvent(QShowEvent *e)
{
  fitView();
}

void
ChatDialog::fitView()
{
  QRectF rect = m_scene->itemsBoundingRect();
  m_scene->setSceneRect(rect);
  treeViewer->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
}
