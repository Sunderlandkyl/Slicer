/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Kitware Inc.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Jean-Christophe Fillion-Robin, Kitware Inc.
  and was partially funded by NIH grant 3P41RR013218-12S1

==============================================================================*/

#include "vtkSlicerConfigure.h" // For Slicer_BUILD_WEBENGINE_SUPPORT

// Qt includes
#include <QComboBox>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QTextBrowser>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTimer>
#include <QTimerEvent>
#include <QToolButton>
#include <QUrlQuery>
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
# include <QWebEngineHistory>
# include <QWebEnginePage>
# include <QWebEngineView>
#endif

// CTK includes
#include <ctkSearchBox.h>
#include <ctkMessageBox.h>

// QtGUI includes
#include <qSlicerApplication.h>
#include "qSlicerExtensionsLocalWidget.h"
#include "qSlicerExtensionsManagerWidget.h"
#include "qSlicerExtensionsManagerModel.h"
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
# include "qSlicerExtensionsServerWidget.h"
#endif
#include "ui_qSlicerExtensionsActionsWidget.h"
#include "ui_qSlicerExtensionsManagerWidget.h"
#include "ui_qSlicerExtensionsToolsWidget.h"

// --------------------------------------------------------------------------
namespace
{

#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
QString jsQuote(QString text)
{
  // NOTE: This assumes that 'text' does not contain '\r' or other control characters
  static QRegularExpression reSpecialCharacters("([\'\"\\\\])");
  text.replace(reSpecialCharacters, "\\\\1").replace("\n", "\\n");
  return QString("\'%1\'").arg(text);
}
#endif

// --------------------------------------------------------------------------
void invalidateSizeHint(QToolButton* button)
{
  // Invalidate cached size hint of QToolButton... this seems to be necessary
  // to get the initially visible button to have the correct hint for having a
  // menu indicator included; otherwise the configure buttons end up with
  // different sizes, causing the UI to "jump" when switching tabs
  //
  // NOTE: This depends on some knowledge of the QToolButton internals;
  //       specifically, that changing the toolButtonStyle will invalidate the
  //       hint (given that we are toggling visibility of the text, it seems
  //       pretty safe to assume this will always do the trick)
  //
  // See https://bugreports.qt-project.org/browse/QTBUG-38949
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
}

//---------------------------------------------------------------------------
void setThemeIcon(QAbstractButton* button, const QString& name)
{
  // TODO: Can do this in the .ui once Qt 4.8 is required
  button->setIcon(QIcon::fromTheme(name, button->icon()));
}

//---------------------------------------------------------------------------
void setThemeIcon(QAction* action, const QString& name)
{
  // TODO: Can do this in the .ui once Qt 4.8 is required
  action->setIcon(QIcon::fromTheme(name, action->icon()));
}

// --------------------------------------------------------------------------
class qSlicerExtensionsActionsWidget
  : public QStackedWidget
  , public Ui_qSlicerExtensionsActionsWidget
{
public:
  qSlicerExtensionsActionsWidget(QWidget* parent = nullptr)
    : QStackedWidget(parent)
  {
    this->setupUi(this);
  }
};

// --------------------------------------------------------------------------
class qSlicerExtensionsToolsWidget
  : public QWidget
  , public Ui_qSlicerExtensionsToolsWidget
{
public:
  qSlicerExtensionsToolsWidget(QWidget* parent = nullptr)
    : QWidget(parent)
  {
    this->setupUi(this);

    setThemeIcon(this->ConfigureButton, "configure");
    setThemeIcon(this->CheckForUpdatesAction, "view-refresh");

    const QIcon searchIcon = QIcon::fromTheme("edit-find", QPixmap(":/Icons/Search.png"));
    const QIcon clearIcon = QIcon::fromTheme(this->layoutDirection() == Qt::LeftToRight //
                                               ? "edit-clear-locationbar-rtl"           //
                                               : "edit-clear-locationbar-ltr",          //
                                             this->SearchBox->clearIcon());

    const QFontMetrics fm = this->SearchBox->fontMetrics();
    const int searchWidth = 24 * fm.averageCharWidth() + 40;

    this->SearchBox->setClearIcon(clearIcon);
    this->SearchBox->setSearchIcon(searchIcon);
    this->SearchBox->setShowSearchIcon(true);
    this->SearchBox->setFixedWidth(searchWidth);

    // manage
    QMenu* configureMenu = new QMenu(this);
    configureMenu->addAction(this->CheckForUpdatesAction);
    configureMenu->addAction(this->AutoUpdateCheckAction);
    configureMenu->addAction(this->AutoUpdateInstallAction);
    configureMenu->addAction(this->AutoInstallDependenciesAction);
    configureMenu->addAction(this->EditBookmarksAction);
    configureMenu->addAction(this->OpenExtensionsCatalogWebsiteAction);
    // Of an extensions catalog, which has no web application: the page that comes with the application,
    // in place of the list. Shown when there is a choice (qSlicerExtensionsManagerWidgetPrivate::applyCatalogView).
    this->CatalogPageAction = new QAction(qSlicerExtensionsManagerWidget::tr("Show the extensions as a web page"), this);
    this->CatalogPageAction->setCheckable(true);
    this->CatalogPageAction->setVisible(false);
    configureMenu->addAction(this->CatalogPageAction);

    this->ConfigureButton->setMenu(configureMenu);
    invalidateSizeHint(this->ConfigureButton);
  }
  QAction* CatalogPageAction{ nullptr };
};

// --------------------------------------------------------------------------
/// What the server says of an extension, with the images it names, which are retrieved by who shows this.
class qSlicerExtensionDetailsBrowser : public QTextBrowser
{
public:
  qSlicerExtensionDetailsBrowser(QWidget* parent = nullptr)
    : QTextBrowser(parent)
  {
  }
  QVariant loadResource(int type, const QUrl& name) override
  {
    if (type == QTextDocument::ImageResource && (name.scheme() == "http" || name.scheme() == "https"))
    {
      // What has been retrieved, or nothing yet: never retrieved from here
      return this->Images.contains(name.toString()) ? QVariant(this->Images.value(name.toString())) : QVariant();
    }
    return this->QTextBrowser::loadResource(type, name);
  }
  QHash<QString, QImage> Images;
};

} // namespace

//-----------------------------------------------------------------------------
class qSlicerExtensionsManagerWidgetPrivate : public Ui_qSlicerExtensionsManagerWidget
{
  Q_DECLARE_PUBLIC(qSlicerExtensionsManagerWidget);

protected:
  qSlicerExtensionsManagerWidget* const q_ptr;

public:
  qSlicerExtensionsManagerWidgetPrivate(qSlicerExtensionsManagerWidget& object);
  void init();
  bool setBatchProcessing(bool newMode);

#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  qSlicerExtensionsServerWidget* ExtensionsManageBrowser;
  qSlicerExtensionsServerWidget* ExtensionsServerWidget;
#endif

  /// Whether the application itself lists the extensions that the server offers (ExtensionsCatalogWidget),
  /// because the server has no web application to browse them with.
  /// Whether the server is an extensions catalog, which has no web application.
  bool isCatalogServer() const;
  /// Show what an extensions catalog offers as the list of the application or as the page that comes
  /// with it, as chosen (Extensions/CatalogView setting: "list", the default, or "page").
  void applyCatalogView();
  void updateCatalogCategories();
  void showExtensionDetails(const QString& extensionName);
  QComboBox* CatalogCategoryComboBox{ nullptr };
  qSlicerExtensionDetailsBrowser* ExtensionDetailsBrowser{ nullptr };
  QString ExtensionDetailsShown;
  QNetworkAccessManager DetailsImageManager;
  QStringList DetailsImagesRequested;
  bool isCatalogListed() const;
  /// Retrieve what the server offers, unless it has been retrieved in the last minutes: what is listed
  /// is what was last retrieved, and nothing retrieves it otherwise unless updates are checked for automatically.
  void refreshCatalog();
  qSlicerExtensionsLocalWidget* ExtensionsCatalogWidget{ nullptr };
  QLabel* ExtensionsCatalogNoteLabel{ nullptr };
  QDateTime CatalogRefreshTime;

  qSlicerExtensionsToolsWidget* ToolsWidget;
  QString LastInstallWidgetSearchText;
  QUrl LastInstallWidgetUrl;
  int SearchTimerId;
  bool IsBatchProcessing{ false }; // in the process of installing or updating multiple extensions - should not close the widget

  QMessageBox* MessageWidget{ nullptr };
  QStringList Messages;
  QTimer* MessageWidgetAutoCloseTimer;
};

// --------------------------------------------------------------------------
qSlicerExtensionsManagerWidgetPrivate::qSlicerExtensionsManagerWidgetPrivate(qSlicerExtensionsManagerWidget& object)
  : q_ptr(&object)
  , ToolsWidget(nullptr)
  , SearchTimerId(0)
{
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  this->ExtensionsManageBrowser = nullptr;
  this->ExtensionsServerWidget = nullptr;
#endif
}

// --------------------------------------------------------------------------
bool qSlicerExtensionsManagerWidgetPrivate::isCatalogServer() const
{
  Q_Q(const qSlicerExtensionsManagerWidget);
  return q->extensionsManagerModel() && q->extensionsManagerModel()->serverAPI() == qSlicerExtensionsManagerModel::GitHub_v1;
}

// --------------------------------------------------------------------------
bool qSlicerExtensionsManagerWidgetPrivate::isCatalogListed() const
{
  if (!this->isCatalogServer())
  {
    return false;
  }
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  return QSettings().value("Extensions/CatalogView", "list").toString() != "page";
#else
  return true;
#endif
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidgetPrivate::applyCatalogView()
{
  Q_Q(qSlicerExtensionsManagerWidget);
  qSlicerExtensionsManagerModel* model = q->extensionsManagerModel();
  const bool listed = this->isCatalogListed();
  // The view that is not shown keeps the model: the model goes on telling it of what happens, whatever
  // it has been told of the model (neither view stops listening to a model it is told to leave).
  this->ExtensionsCatalogWidget->setExtensionsManagerModel(this->isCatalogServer() ? model : nullptr);
  this->ExtensionsCatalogWidget->setVisible(listed);
  this->CatalogCategoryComboBox->setVisible(listed);
  if (!listed)
  {
    this->ExtensionDetailsBrowser->setVisible(false);
  }
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  // The browser is not shown when the application lists the extensions itself.
  this->ExtensionsServerWidget->setExtensionsManagerModel(model);
  this->ExtensionsServerWidget->setVisible(!listed);
  if (!listed && model && this->isCatalogServer())
  {
    this->ExtensionsServerWidget->refresh();
  }
  QSignalBlocker blocker(this->ToolsWidget->CatalogPageAction);
  this->ToolsWidget->CatalogPageAction->setVisible(this->isCatalogServer());
  this->ToolsWidget->CatalogPageAction->setChecked(this->isCatalogServer() && !listed);
#endif
  if (listed)
  {
    this->updateCatalogCategories();
  }
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidgetPrivate::updateCatalogCategories()
{
  Q_Q(qSlicerExtensionsManagerWidget);
  if (!q->extensionsManagerModel())
  {
    return;
  }
  QStringList categories;
  for (const QString& extensionName : q->extensionsManagerModel()->serverExtensions())
  {
    const QString category = q->extensionsManagerModel()->extensionMetadata(extensionName, qSlicerExtensionsManagerModel::MetadataServer).value("category").toString();
    for (const QString& name : category.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts))
    {
      if (!categories.contains(name.trimmed()))
      {
        categories << name.trimmed();
      }
    }
  }
  categories.sort(Qt::CaseInsensitive);
  const QString current = this->CatalogCategoryComboBox->currentData().toString();
  QSignalBlocker blocker(this->CatalogCategoryComboBox);
  this->CatalogCategoryComboBox->clear();
  this->CatalogCategoryComboBox->addItem(qSlicerExtensionsManagerWidget::tr("All categories"), QString());
  for (const QString& category : categories)
  {
    this->CatalogCategoryComboBox->addItem(category, category);
  }
  const int index = this->CatalogCategoryComboBox->findData(current);
  this->CatalogCategoryComboBox->setCurrentIndex(index < 0 ? 0 : index);
  if (index < 0 && !current.isEmpty())
  {
    // The category that was chosen is no more
    this->ExtensionsCatalogWidget->setCategoryFilter(QString());
  }
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidgetPrivate::showExtensionDetails(const QString& extensionName)
{
  Q_Q(qSlicerExtensionsManagerWidget);
  if (!q->extensionsManagerModel())
  {
    return;
  }
  const qSlicerExtensionsManagerModel::ExtensionMetadataType metadata =
    q->extensionsManagerModel()->extensionMetadata(extensionName, qSlicerExtensionsManagerModel::MetadataServer);
  this->ExtensionDetailsShown = extensionName;

  // What an extension says of itself is text, with <br> for its line breaks.
  auto text = [](const QString& value) { return value.toHtmlEscaped().replace("&lt;br&gt;", "<br>", Qt::CaseInsensitive); };
  auto isAddress = [](const QString& value) { return value.startsWith("http://") || value.startsWith("https://"); };
  QStringList images;
  QString html = QString("<p align=\"right\"><a href=\"close:\">%1</a></p>").arg(qSlicerExtensionsManagerWidget::tr("Close"));
  const QString icon = metadata.value("iconurl").toString();
  html += "<table><tr>";
  if (isAddress(icon))
  {
    images << icon;
    html += QString("<td><img src=\"%1\" width=\"64\" height=\"64\"></td>").arg(icon.toHtmlEscaped());
  }
  html += QString("<td>&nbsp;</td><td><h2>%1</h2>%2</td></tr></table>").arg(extensionName.toHtmlEscaped(), text(metadata.value("category").toString()));
  if (metadata.value("tier").toString() == "0")
  {
    html += QString("<p><b>%1</b></p>").arg(qSlicerExtensionsManagerWidget::tr("Not reviewed: this extension is not in the Slicer extensions index."));
  }
  html += QString("<p>%1</p>").arg(text(metadata.value("description").toString()));
  html += "<table>";
  auto row = [&html](const QString& name, const QString& value)
  {
    if (!value.isEmpty())
    {
      html += QString("<tr><td><i>%1</i>&nbsp;&nbsp;</td><td>%2</td></tr>").arg(name, value);
    }
  };
  auto link = [&isAddress](const QString& value) { return isAddress(value) ? QString("<a href=\"%1\">%1</a>").arg(value.toHtmlEscaped()) : QString(); };
  const QString depends = metadata.value("depends").toString();
  row(qSlicerExtensionsManagerWidget::tr("Contributors"), text(metadata.value("contributors").toString()));
  row(qSlicerExtensionsManagerWidget::tr("Home page"), link(metadata.value("homepage").toString()));
  row(qSlicerExtensionsManagerWidget::tr("Source"), link(metadata.value("scmurl").toString()));
  row(qSlicerExtensionsManagerWidget::tr("Revision"), metadata.value("revision").toString().left(10).toHtmlEscaped());
  row(qSlicerExtensionsManagerWidget::tr("Updated"), metadata.value("updated").toString().left(10).toHtmlEscaped());
  row(qSlicerExtensionsManagerWidget::tr("Requires"), depends == "NA" ? QString() : depends.toHtmlEscaped());
  html += "</table>";
  for (const QString& screenshot : metadata.value("screenshots").toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
  {
    if (isAddress(screenshot))
    {
      images << screenshot;
      html += QString("<p><img src=\"%1\"></p>").arg(screenshot.toHtmlEscaped());
    }
  }
  this->ExtensionDetailsBrowser->setHtml(html);
  this->ExtensionDetailsBrowser->setVisible(true);

  // The images are retrieved once, and the details shown again when one is there.
  for (const QString& image : images)
  {
    if (this->DetailsImagesRequested.contains(image))
    {
      continue;
    }
    this->DetailsImagesRequested << image;
    QNetworkRequest request{ QUrl(image) };
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = this->DetailsImageManager.get(request);
    QObject::connect(reply, &QNetworkReply::finished, q, [this, reply, image]()
    {
      reply->deleteLater();
      QImage loaded;
      if (reply->error() != QNetworkReply::NoError || !loaded.loadFromData(reply->readAll()))
      {
        return;
      }
      if (loaded.width() > 640)
      {
        loaded = loaded.scaledToWidth(640, Qt::SmoothTransformation);
      }
      this->ExtensionDetailsBrowser->Images.insert(image, loaded);
      if (this->ExtensionDetailsBrowser->isVisible() && !this->ExtensionDetailsShown.isEmpty())
      {
        this->showExtensionDetails(this->ExtensionDetailsShown);
      }
    });
  }
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidgetPrivate::refreshCatalog()
{
  Q_Q(qSlicerExtensionsManagerWidget);
  if (!this->isCatalogListed())
  {
    return;
  }
  const QDateTime now = QDateTime::currentDateTimeUtc();
  if (this->CatalogRefreshTime.isValid() && this->CatalogRefreshTime.secsTo(now) < 300)
  {
    return;
  }
  this->CatalogRefreshTime = now;
  // In the background: the list is filled when the answer is there (modelUpdated).
  q->extensionsManagerModel()->updateExtensionsMetadataFromServer(/* force= */ true, /* waitForCompletion= */ false);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidgetPrivate::init()
{
  Q_Q(qSlicerExtensionsManagerWidget);

  this->setupUi(q);

  // The extensions that the server offers, listed by the application: in place of the browser
  // of the "Install Extensions" tab when the server has no web application (setExtensionsManagerModel).
  this->ExtensionsCatalogNoteLabel = new QLabel(this->InstallExtensionsTab);
  this->ExtensionsCatalogNoteLabel->setObjectName("ExtensionsCatalogNoteLabel");
  this->ExtensionsCatalogNoteLabel->setWordWrap(true);
  this->ExtensionsCatalogNoteLabel->setMargin(6);
  this->ExtensionsCatalogNoteLabel->setVisible(false);
  this->InstallExtensionsTabLayout->addWidget(this->ExtensionsCatalogNoteLabel);
  this->ExtensionsCatalogWidget = new qSlicerExtensionsLocalWidget(this->InstallExtensionsTab);
  this->ExtensionsCatalogWidget->setObjectName("ExtensionsCatalogWidget");
  this->ExtensionsCatalogWidget->setServerExtensionsListed(true);
  this->ExtensionsCatalogWidget->setVisible(false);
  this->CatalogCategoryComboBox = new QComboBox(this->InstallExtensionsTab);
  this->CatalogCategoryComboBox->setObjectName("CatalogCategoryComboBox");
  this->CatalogCategoryComboBox->setVisible(false);
  this->InstallExtensionsTabLayout->addWidget(this->CatalogCategoryComboBox);
  this->InstallExtensionsTabLayout->addWidget(this->ExtensionsCatalogWidget, 3);
  // What the server says of the extension whose "More..." link is activated
  this->ExtensionDetailsBrowser = new qSlicerExtensionDetailsBrowser(this->InstallExtensionsTab);
  this->ExtensionDetailsBrowser->setObjectName("ExtensionDetailsBrowser");
  this->ExtensionDetailsBrowser->setOpenLinks(false);
  this->ExtensionDetailsBrowser->setVisible(false);
  this->InstallExtensionsTabLayout->addWidget(this->ExtensionDetailsBrowser, 2);
  QObject::connect(this->CatalogCategoryComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), q, [this](int)
  {
    this->ExtensionsCatalogWidget->setCategoryFilter(this->CatalogCategoryComboBox->currentData().toString());
  });
  QObject::connect(this->ExtensionsCatalogWidget, &qSlicerExtensionsLocalWidget::extensionDetailsRequested, q, [this](const QString& extensionName)
  {
    this->showExtensionDetails(extensionName);
  });
  QObject::connect(this->ExtensionDetailsBrowser, &QTextBrowser::anchorClicked, q, [this](const QUrl& url)
  {
    if (url.scheme() == "close")
    {
      this->ExtensionDetailsBrowser->setVisible(false);
      this->ExtensionDetailsShown.clear();
    }
    else
    {
      QDesktopServices::openUrl(url);
    }
  });

#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  // Setup browser for "Install Extensions" tab
  this->ExtensionsServerWidget = new qSlicerExtensionsServerWidget(this->InstallExtensionsTab);
  this->ExtensionsServerWidget->setObjectName("ExtensionsServerWidget");
  this->InstallExtensionsTabLayout->addWidget(this->ExtensionsServerWidget);

  // Setup browser for "Manage Extensions" tab
  this->ExtensionsManageBrowser = new qSlicerExtensionsServerWidget();
  this->ExtensionsManageBrowser->setObjectName("ExtensionsManageBrowser");
  this->ManageExtensionsPager->addWidget(this->ExtensionsManageBrowser);
  this->ExtensionsManageBrowser->setBrowsingEnabled(false);
  this->ExtensionsManageBrowser->webView()->load(QUrl(/*no tr*/ "about:"));
#endif

  qSlicerExtensionsActionsWidget* actionsWidget = new qSlicerExtensionsActionsWidget;

  // Back and forward buttons
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  actionsWidget->ManageBackButton->setDefaultAction(this->ExtensionsManageBrowser->webView()->pageAction(QWebEnginePage::Back));
  actionsWidget->ManageForwardButton->setDefaultAction(this->ExtensionsManageBrowser->webView()->pageAction(QWebEnginePage::Forward));
  actionsWidget->InstallBackButton->setDefaultAction(this->ExtensionsServerWidget->webView()->pageAction(QWebEnginePage::Back));
  actionsWidget->InstallForwardButton->setDefaultAction(this->ExtensionsServerWidget->webView()->pageAction(QWebEnginePage::Forward));
#endif

  this->tabWidget->setCornerWidget(actionsWidget, Qt::TopLeftCorner);

  // Search field and configure button
  this->ToolsWidget = new qSlicerExtensionsToolsWidget;

  QObject::connect(this->ToolsWidget->AutoUpdateCheckAction, SIGNAL(toggled(bool)), q, SLOT(setAutoUpdateCheck(bool)));
  QObject::connect(this->ToolsWidget->AutoUpdateInstallAction, SIGNAL(toggled(bool)), q, SLOT(setAutoUpdateInstall(bool)));
  QObject::connect(this->ToolsWidget->AutoInstallDependenciesAction, SIGNAL(toggled(bool)), q, SLOT(setAutoInstallDependencies(bool)));
  QObject::connect(this->ToolsWidget->CatalogPageAction, &QAction::toggled, q, [this, q](bool page)
  {
    QSettings().setValue("Extensions/CatalogView", page ? "page" : "list");
    this->applyCatalogView();
    q->onModelUpdated();
    this->LastInstallWidgetSearchText.clear();
    q->processSearchTextChange();
  });

  this->tabWidget->setCornerWidget(this->ToolsWidget, Qt::TopRightCorner);

  QObject::connect(this->tabWidget, SIGNAL(currentChanged(int)), actionsWidget, SLOT(setCurrentIndex(int)));
  QObject::connect(this->ExtensionsLocalWidget, SIGNAL(linkActivated(QUrl)), q, SLOT(onManageLinkActivated(QUrl)));

#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  QObject::connect(this->ExtensionsManageBrowser->webView(), SIGNAL(urlChanged(QUrl)), q, SLOT(onManageUrlChanged(QUrl)));
  QObject::connect(this->ToolsWidget->SearchBox, SIGNAL(textEdited(QString)), q, SLOT(onSearchTextChanged(QString)));
  QObject::connect(this->ExtensionsServerWidget->webView(), SIGNAL(urlChanged(QUrl)), q, SLOT(onInstallUrlChanged(QUrl)));
#endif

  QObject::connect(this->tabWidget, SIGNAL(currentChanged(int)), q, SLOT(onCurrentTabChanged(int)));
  QObject::connect(this->ToolsWidget->CheckForUpdatesAction, SIGNAL(triggered(bool)), q, SLOT(onCheckForUpdatesTriggered()));
  QObject::connect(this->ToolsWidget->EditBookmarksAction, SIGNAL(triggered(bool)), q, SLOT(onEditBookmarksTriggered()));
  qSlicerApplication* app = qSlicerApplication::application();
  QObject::connect(this->ToolsWidget->OpenExtensionsCatalogWebsiteAction, SIGNAL(triggered(bool)), app, SLOT(openExtensionsCatalogWebsite()));
  QObject::connect(this->ToolsWidget->CheckForUpdatesButton, SIGNAL(clicked()), q, SLOT(onCheckForUpdatesTriggered()));
  QObject::connect(this->ToolsWidget->InstallUpdatesButton, SIGNAL(clicked()), q, SLOT(onInstallUpdatesTriggered()));
  QObject::connect(this->ToolsWidget->InstallBookmarkedButton, SIGNAL(clicked()), q, SLOT(onInstallBookmarkedTriggered()));
  QObject::connect(this->ToolsWidget->InstallFromFileButton, SIGNAL(clicked()), q, SLOT(onInstallFromFileTriggered()));

  this->MessageWidget = new QMessageBox(q);
  this->MessageWidget->setWindowTitle(qSlicerExtensionsManagerWidget::tr("Extensions Manager"));
  this->MessageWidgetAutoCloseTimer = new QTimer(q);
  this->MessageWidgetAutoCloseTimer->setInterval(500);
  this->MessageWidgetAutoCloseTimer->setSingleShot(true);
  QObject::connect(this->MessageWidgetAutoCloseTimer, SIGNAL(timeout()), this->MessageWidget, SLOT(accept()));
  QObject::connect(this->MessageWidget, SIGNAL(finished(int)), q, SLOT(onMessagesAcknowledged()));
}

// --------------------------------------------------------------------------
bool qSlicerExtensionsManagerWidgetPrivate::setBatchProcessing(bool newMode)
{
  Q_Q(qSlicerExtensionsManagerWidget);
  bool wasBatchProcessing = this->IsBatchProcessing;
  this->IsBatchProcessing = newMode;
  if (wasBatchProcessing != this->IsBatchProcessing)
  {
    if (!this->IsBatchProcessing)
    {
      QApplication::restoreOverrideCursor();
    }
    emit q->inBatchProcessing(this->IsBatchProcessing);
    if (this->IsBatchProcessing)
    {
      QApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
    }
  }
  return wasBatchProcessing;
}

// --------------------------------------------------------------------------
// qSlicerExtensionsManagerWidget methods

// --------------------------------------------------------------------------
qSlicerExtensionsManagerWidget::qSlicerExtensionsManagerWidget(QWidget* _parent)
  : Superclass(_parent)
  , d_ptr(new qSlicerExtensionsManagerWidgetPrivate(*this))
{
  Q_D(qSlicerExtensionsManagerWidget);
  d->init();
}

// --------------------------------------------------------------------------
qSlicerExtensionsManagerWidget::~qSlicerExtensionsManagerWidget() {}

// --------------------------------------------------------------------------
qSlicerExtensionsManagerModel* qSlicerExtensionsManagerWidget::extensionsManagerModel() const
{
  Q_D(const qSlicerExtensionsManagerWidget);
  return d->ExtensionsLocalWidget->extensionsManagerModel();
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::setExtensionsManagerModel(qSlicerExtensionsManagerModel* model)
{
  Q_D(qSlicerExtensionsManagerWidget);

  if (this->extensionsManagerModel() == model)
  {
    return;
  }

  disconnect(this, SLOT(onModelUpdated()));
  disconnect(this, SLOT(onMessageLogged(QString, ctkErrorLogLevel::LogLevels)));

  d->ExtensionsLocalWidget->setExtensionsManagerModel(model);
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  d->ExtensionsManageBrowser->setExtensionsManagerModel(model);
#endif
  // Of an extensions catalog: its list or its page, as chosen; the web application of another server.
  d->applyCatalogView();

  if (model)
  {
    this->updateAutoUpdateWidgetsFromModel();

    this->onModelUpdated();
    connect(model, SIGNAL(modelUpdated()), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(messageLogged(QString, ctkErrorLogLevel::LogLevels)), this, SLOT(onMessageLogged(QString, ctkErrorLogLevel::LogLevels)));
    connect(model, SIGNAL(extensionInstalled(QString)), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(extensionUninstalled(QString)), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(extensionScheduledForUpdate(QString)), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(extensionCancelledScheduleForUpdate(QString)), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(extensionBookmarkedChanged(QString, bool)), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(extensionUpdateAvailable(QString)), this, SLOT(onModelUpdated()));
    connect(model, SIGNAL(autoUpdateSettingsChanged()), this, SLOT(updateAutoUpdateWidgetsFromModel()));
    // Whether it succeeded or not: there may be something to say of it (serverNote)
    connect(model, SIGNAL(updateExtensionsMetadataFromServerCompleted(bool)), this, SLOT(onModelUpdated()));
    d->refreshCatalog();

    // Ensure extension metadata is retrieved from the server or cache.
    // This is performed early during application startup, except the very first time after install,
    // because then the default extension server address is set later during startup.
    if (!model->lastUpdateTimeExtensionsMetadataFromServer().isValid())
    {
      model->updateExtensionsMetadataFromServer();
    }
  }
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::updateAutoUpdateWidgetsFromModel()
{
  Q_D(qSlicerExtensionsManagerWidget);
  QSignalBlocker blocker1(d->ToolsWidget->AutoUpdateInstallAction);
  QSignalBlocker blocker2(d->ToolsWidget->AutoUpdateCheckAction);
  QSignalBlocker blocker3(d->ToolsWidget->AutoInstallDependenciesAction);
  d->ToolsWidget->AutoUpdateCheckAction->setChecked(this->extensionsManagerModel()->autoUpdateCheck());
  d->ToolsWidget->AutoUpdateInstallAction->setChecked(this->extensionsManagerModel()->autoUpdateInstall());
  d->ToolsWidget->AutoInstallDependenciesAction->setChecked(this->extensionsManagerModel()->autoInstallDependencies());
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::refreshInstallWidget()
{
  Q_D(qSlicerExtensionsManagerWidget);
  if (!this->extensionsManagerModel())
  {
    return;
  }
  this->extensionsManagerModel()->updateExtensionsMetadataFromServer();
  this->extensionsManagerModel()->updateModel();
  d->refreshCatalog();

#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  if (!d->isCatalogListed())
  {
    d->ExtensionsServerWidget->refresh();
  }
#endif
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onModelUpdated()
{
  Q_D(qSlicerExtensionsManagerWidget);

  int manageExtensionsTabIndex = d->tabWidget->indexOf(d->ManageExtensionsTab);
  int managedExtensionsCount = this->extensionsManagerModel()->managedExtensionsCount();

  // What there is to say of what the server offers, for example that it is for another revision
  QString serverNote = d->isCatalogListed() ? this->extensionsManagerModel()->serverNote() : QString();
  if (serverNote.isEmpty() && d->isCatalogListed() && this->extensionsManagerModel()->serverExtensions().isEmpty())
  {
    // An empty list is not left unexplained
    serverNote = this->extensionsManagerModel()->lastUpdateTimeExtensionsMetadataFromServer().isValid() //
                   ? tr("The server offers no extension for this version of the application.")
                   : tr("Retrieving the list of extensions...");
  }
  d->ExtensionsCatalogNoteLabel->setText(serverNote);
  d->ExtensionsCatalogNoteLabel->setVisible(!serverNote.isEmpty());
  if (d->isCatalogListed())
  {
    d->updateCatalogCategories();
  }

  // Get the list of extensions that have update available but not updated yet
  QStringList extensionsToUpdate = this->extensionsManagerModel()->availableUpdateExtensions();
  const QStringList& extensionsAlreadyScheduledForUpdate = this->extensionsManagerModel()->scheduledForUpdateExtensions();
  for (const QString& extensionName : extensionsAlreadyScheduledForUpdate)
  {
    extensionsToUpdate.removeAll(extensionName);
  }
  int extensionUpdates = extensionsToUpdate.size();
  if (extensionUpdates > 0)
  {
    d->ToolsWidget->InstallUpdatesButton->setText(tr("Update all (%1)").arg(extensionUpdates));
    d->ToolsWidget->InstallUpdatesButton->setVisible(true);
    d->ToolsWidget->CheckForUpdatesButton->setVisible(false);
  }
  else
  {
    d->ToolsWidget->InstallUpdatesButton->setVisible(false);
    d->ToolsWidget->CheckForUpdatesButton->setVisible(true);
    d->ToolsWidget->CheckForUpdatesButton->setEnabled(this->extensionsManagerModel()->managedExtensionsCount() > 0);
  }

  int foundNonInstalledBookmarkedExtension = 0;
  QStringList bookmarkedExtensions = this->extensionsManagerModel()->bookmarkedExtensions();
  for (const QString& extensionName : bookmarkedExtensions)
  {
    if (this->extensionsManagerModel()->isExtensionInstalled(extensionName))
    {
      // already installed
      continue;
    }
    const qSlicerExtensionsManagerModel::ExtensionMetadataType& metadataFromServer =
      this->extensionsManagerModel()->extensionMetadata(extensionName, qSlicerExtensionsManagerModel::MetadataServer);
    if (metadataFromServer["revision"].toString().isEmpty())
    {
      // not available on server
      continue;
    }
    foundNonInstalledBookmarkedExtension++;
  }
  if (foundNonInstalledBookmarkedExtension > 0)
  {
    d->ToolsWidget->InstallBookmarkedButton->setText(tr("Install bookmarked (%1)").arg(foundNonInstalledBookmarkedExtension));
    d->ToolsWidget->InstallBookmarkedButton->setEnabled(true);
  }
  else
  {
    d->ToolsWidget->InstallBookmarkedButton->setText(tr("Install bookmarked"));
    d->ToolsWidget->InstallBookmarkedButton->setEnabled(false);
  }

  d->tabWidget->setTabText(manageExtensionsTabIndex,
                           //: %1 represents the number of managed extensions
                           tr("Manage Extensions (%1)").arg(managedExtensionsCount));

  if (managedExtensionsCount == 0)
  {
    d->tabWidget->setTabEnabled(manageExtensionsTabIndex, false);
    d->tabWidget->setCurrentWidget(d->InstallExtensionsTab);
  }
  else
  {
    d->tabWidget->setTabEnabled(manageExtensionsTabIndex, true);
  }
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onCheckForUpdatesTriggered()
{
  Q_D(qSlicerExtensionsManagerWidget);
  bool wasBatchProcessing = d->setBatchProcessing(true);
  this->extensionsManagerModel()->updateExtensionsMetadataFromServer(true, true);
  this->extensionsManagerModel()->checkForExtensionsUpdates();
  d->setBatchProcessing(wasBatchProcessing);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onEditBookmarksTriggered()
{
  Q_D(qSlicerExtensionsManagerWidget);
  bool ok = false;
  QStringList oldList = this->extensionsManagerModel()->bookmarkedExtensions();
  QString newStr = QInputDialog::getMultiLineText(this, tr("Bookmarked extensions"), tr("List of bookmarked extensions:"), oldList.join("\n"), &ok);
  if (!ok)
  {
    // Cancel clicked
    return;
  }
  // Split along whitespaces and common separator characters
  QStringList newList = newStr.split(QRegularExpression("\\s+|,|;"), Qt::SkipEmptyParts);
  newList.removeDuplicates();

  // Update bookmarks
  QSettings settings;
  settings.setValue(/*no tr*/ "Extensions/Bookmarked", newList);
  this->extensionsManagerModel()->updateModel();
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onCurrentTabChanged(int index)
{
  Q_D(qSlicerExtensionsManagerWidget);
  Q_UNUSED(index);
  if (d->tabWidget->currentWidget() == d->ManageExtensionsTab)
  {
    d->ToolsWidget->SearchBox->setEnabled(true);
  }
  else if (d->tabWidget->currentWidget() == d->InstallExtensionsTab && d->isCatalogListed())
  {
    d->ToolsWidget->SearchBox->setEnabled(true);
    d->refreshCatalog();
  }
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  else if (d->tabWidget->currentWidget() == d->InstallExtensionsTab)
  {
    QWebEngineHistory* history = d->ExtensionsManageBrowser->webView()->history();
    if (history->canGoBack())
    {
      history->goToItem(history->items().first());
    }
    bool isCatalogPage = false;
    int serverAPI = this->extensionsManagerModel()->serverAPI();
    if (serverAPI == qSlicerExtensionsManagerModel::Girder_v1 || serverAPI == qSlicerExtensionsManagerModel::GitHub_v1)
    {
      isCatalogPage = d->LastInstallWidgetUrl.path().contains("/catalog");
    }
    else
    {
      qWarning() << Q_FUNC_INFO << " failed: missing implementation for serverAPI" << serverAPI;
    }
    if (isCatalogPage)
    {
      d->ToolsWidget->SearchBox->setEnabled(true);
      // When URL is changed because user clicked on a link then we want the search text
      // to be reset. However, when user is entering text in the searchbox (it has the focus)
      // then we must not overwrite the search text.
      if (!d->ToolsWidget->SearchBox->hasFocus())
      {
        QString lastSearchTextLoaded;
        if (serverAPI == qSlicerExtensionsManagerModel::Girder_v1)
        {
          lastSearchTextLoaded = QUrlQuery(d->LastInstallWidgetUrl).queryItemValue("q");
        }
        else
        {
          lastSearchTextLoaded = d->ToolsWidget->SearchBox->text();
        }
        d->ToolsWidget->SearchBox->setText(lastSearchTextLoaded);
      }
    }
    else
    {
      d->ToolsWidget->SearchBox->setEnabled(false);
    }
  }
#endif
  this->processSearchTextChange();
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onManageLinkActivated(const QUrl& link)
{
  Q_D(qSlicerExtensionsManagerWidget);
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  d->ManageExtensionsPager->setCurrentIndex(1);
  d->ExtensionsManageBrowser->webView()->load(link);
#else
  Q_UNUSED(link);
#endif
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onManageUrlChanged(const QUrl& newUrl)
{
  Q_D(qSlicerExtensionsManagerWidget);
  d->ManageExtensionsPager->setCurrentIndex(newUrl.scheme() == /*no tr*/ "about" ? 0 : 1);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onInstallUrlChanged(const QUrl& newUrl)
{
  Q_D(qSlicerExtensionsManagerWidget);
  // refresh tools widget state (it should be only enabled if browsing the appstore)
  bool isCatalogPage = false;
  int serverAPI = this->extensionsManagerModel()->serverAPI();
  QString lastSearchTextLoaded;
  if (serverAPI == qSlicerExtensionsManagerModel::Girder_v1)
  {
    lastSearchTextLoaded = QUrlQuery(newUrl).queryItemValue("q");
    isCatalogPage = newUrl.path().contains("/catalog");
  }
  else if (serverAPI == qSlicerExtensionsManagerModel::GitHub_v1)
  {
    // The page that comes with the application: it is searched as it is, and has no address of its own for it.
    lastSearchTextLoaded = d->ToolsWidget->SearchBox->text();
    isCatalogPage = newUrl.path().contains("/catalog");
  }
  if (isCatalogPage)
  {
    d->ToolsWidget->SearchBox->setEnabled(true);
    // When URL is changed because user clicked on a link then we want the search text
    // to be reset. However, when user is entering text in the searchbox (it has the focus)
    // then we must not overwrite the search text.
    if (!d->ToolsWidget->SearchBox->hasFocus())
    {
      d->ToolsWidget->SearchBox->setText(lastSearchTextLoaded);
    }
  }
  else
  {
    d->ToolsWidget->SearchBox->setEnabled(false);
  }
  d->LastInstallWidgetUrl = newUrl;
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onSearchTextChanged(const QString& newText)
{
  Q_UNUSED(newText);
  Q_D(qSlicerExtensionsManagerWidget);
  if (d->SearchTimerId)
  {
    this->killTimer(d->SearchTimerId);
    d->SearchTimerId = 0;
  }
  d->SearchTimerId = this->startTimer(500);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::processSearchTextChange()
{
  Q_D(qSlicerExtensionsManagerWidget);
  const QString& searchText = d->ToolsWidget->SearchBox->text();
  if (d->tabWidget->currentWidget() == d->ManageExtensionsTab)
  {
    d->ExtensionsLocalWidget->setSearchText(searchText);
  }
  else if (d->tabWidget->currentWidget() == d->InstallExtensionsTab && d->isCatalogListed())
  {
    d->ExtensionsCatalogWidget->setSearchText(searchText);
  }
#ifdef Slicer_BUILD_WEBENGINE_SUPPORT
  else if (d->tabWidget->currentWidget() == d->InstallExtensionsTab)
  {
    if (searchText != d->LastInstallWidgetSearchText)
    {
      int serverAPI = this->extensionsManagerModel()->serverAPI();
      if (serverAPI == qSlicerExtensionsManagerModel::Girder_v1 || serverAPI == qSlicerExtensionsManagerModel::GitHub_v1)
      {
        d->ExtensionsServerWidget->webView()->page()->runJavaScript("app.search(" + jsQuote(searchText) + ");");
      }
      else
      {
        qWarning() << Q_FUNC_INFO << " failed: missing implementation for serverAPI" << serverAPI;
      }
      d->LastInstallWidgetSearchText = searchText;
    }
  }
#endif
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::timerEvent(QTimerEvent* e)
{
  Q_D(qSlicerExtensionsManagerWidget);
  if (e->timerId() == d->SearchTimerId)
  {
    this->processSearchTextChange();
    this->killTimer(d->SearchTimerId);
    d->SearchTimerId = 0;
  }
  QObject::timerEvent(e);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onInstallUpdatesTriggered()
{
  Q_D(qSlicerExtensionsManagerWidget);
  // Save last update check time
  bool wasBatchProcessing = d->setBatchProcessing(true);
  QStringList extensionNames = this->extensionsManagerModel()->availableUpdateExtensions();
  for (const QString& extensionName : extensionNames)
  {
    this->extensionsManagerModel()->scheduleExtensionForUpdate(extensionName);
  }
  d->setBatchProcessing(wasBatchProcessing);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onInstallBookmarkedTriggered()
{
  Q_D(qSlicerExtensionsManagerWidget);
  bool wasBatchProcessing = d->setBatchProcessing(true);
  // Save last update check time
  QStringList extensionNames = this->extensionsManagerModel()->bookmarkedExtensions();
  for (const QString& extensionName : extensionNames)
  {
    if (this->extensionsManagerModel()->isExtensionInstalled(extensionName))
    {
      // already installed
      continue;
    }
    const qSlicerExtensionsManagerModel::ExtensionMetadataType& metadata = this->extensionsManagerModel()->extensionMetadata(extensionName);
    QString extensionId = metadata.value("extension_id").toString();
    if (extensionId.isEmpty())
    {
      // not available on the server
      continue;
    }
    // Do not install dependencies, because installing in incorrect order could result in showing
    // installation confirmation popups. The user may also intentionally not want to install
    // some dependencies.
    bool installDependencies = false;
    this->extensionsManagerModel()->downloadAndInstallExtensionByName(extensionName, installDependencies);
  }
  d->setBatchProcessing(wasBatchProcessing);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onInstallFromFileTriggered()
{
  Q_D(qSlicerExtensionsManagerWidget);
  const QStringList& archiveNames = QFileDialog::getOpenFileNames(
    this, tr("Select extension archive file(s)..."), QString(), tr("Archives") + " (*.zip *.7z *.tar *.tar.gz *.tgz *.tar.bz2 *.tar.xz);;" + tr("All files") + " (*)");
  if (archiveNames.empty())
  {
    return;
  }

  bool wasBatchProcessing = d->setBatchProcessing(true);
  qSlicerExtensionsManagerModel* const model = this->extensionsManagerModel();
  for (const QString& archiveName : archiveNames)
  {
    model->installExtension(archiveName);
  }
  d->setBatchProcessing(wasBatchProcessing);
}

// --------------------------------------------------------------------------
bool qSlicerExtensionsManagerWidget::confirmClose()
{
  Q_D(qSlicerExtensionsManagerWidget);
  QStringList pendingOperations;
  if (this->extensionsManagerModel())
  {
    pendingOperations.append(this->extensionsManagerModel()->activeTasks());
  }
  if (pendingOperations.empty() && !d->IsBatchProcessing)
  {
    return true;
  }

  ctkMessageBox confirmDialog;
  confirmDialog.setText(tr("Install/uninstall/update operations are still in progress:") + "\n- " + pendingOperations.join("\n- ") + "\n\n"
                        + tr("Click OK to wait for them to complete, or choose Ignore to close the Extensions Manager now."));
  confirmDialog.setIcon(QMessageBox::Question);
  confirmDialog.setStandardButtons(QMessageBox::Ok | QMessageBox::Ignore);
  bool closeConfirmed = (confirmDialog.exec() == QMessageBox::Ignore);
  return closeConfirmed;
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::setAutoUpdateCheck(bool toggle)
{
  Q_D(qSlicerExtensionsManagerWidget);
  if (!this->extensionsManagerModel())
  {
    return;
  }
  this->extensionsManagerModel()->setAutoUpdateCheck(toggle);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::setAutoUpdateInstall(bool toggle)
{
  Q_D(qSlicerExtensionsManagerWidget);
  if (!this->extensionsManagerModel())
  {
    return;
  }
  this->extensionsManagerModel()->setAutoUpdateInstall(toggle);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::setAutoInstallDependencies(bool toggle)
{
  Q_D(qSlicerExtensionsManagerWidget);
  if (!this->extensionsManagerModel())
  {
    return;
  }
  this->extensionsManagerModel()->setAutoInstallDependencies(toggle);
}

// --------------------------------------------------------------------------
bool qSlicerExtensionsManagerWidget::isInBatchProcessing()
{
  Q_D(qSlicerExtensionsManagerWidget);
  return d->IsBatchProcessing;
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onMessageLogged(const QString& text, ctkErrorLogLevel::LogLevels level)
{
  Q_D(qSlicerExtensionsManagerWidget);
  if (d->tabWidget->currentWidget() != d->ManageExtensionsTab)
  {
    // only display messages when we are in the extensions tab (the web view uses its own style)
    return;
  }

  bool show = false;
  int delayMsec = 2500; // show messages for 2.5 seconds by default
  QString color;
  if (level == ctkErrorLogLevel::Info)
  {
    show = true;
  }
  else if (level == ctkErrorLogLevel::Warning)
  {
    delayMsec = 10000; // show warning messages for 10 seconds
    color = "darkorange";
    show = true;
    if (d->MessageWidget->icon() != QMessageBox::Critical)
    {
      d->MessageWidget->setIcon(QMessageBox::Warning);
    }
  }
  else if (level == ctkErrorLogLevel::Critical || level == ctkErrorLogLevel::Fatal)
  {
    delayMsec = 10000; // show error messages for 10 seconds
    color = "red";
    show = true;
    d->MessageWidget->setIcon(QMessageBox::Critical);
  }
  if (d->MessageWidgetAutoCloseTimer->remainingTime() < delayMsec)
  {
    d->MessageWidgetAutoCloseTimer->start(delayMsec);
  }
  if (color.isEmpty())
  {
    d->Messages << text;
  }
  else
  {
    d->Messages << QString("<font color=\"%1\">%2</font>").arg(color).arg(text);
  }
  const int maxNumberOfDisplayedMessages = 12;
  if (d->Messages.size() > maxNumberOfDisplayedMessages)
  {
    d->Messages.pop_front();
  }
  d->MessageWidget->setText(d->Messages.join("<p>"));
  if (show)
  {
    d->MessageWidget->show();
  }
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::onMessagesAcknowledged()
{
  Q_D(qSlicerExtensionsManagerWidget);
  d->Messages.clear();
  d->MessageWidget->setIcon(QMessageBox::NoIcon);
}

// --------------------------------------------------------------------------
void qSlicerExtensionsManagerWidget::setFocusToSearchBox()
{
  Q_D(qSlicerExtensionsManagerWidget);
  d->ToolsWidget->SearchBox->setFocus();
}
