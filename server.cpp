#include "server.h"
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <algorithm>
//#include <QCoreApplication>
//#include <QDir>

// مسیر فایل‌های ذخیره‌سازی
static const QString dataFilePath(const QString &fileName) {
    return QString(SERVER_DATA_DIR) + "/" + fileName;
}
const QString USERS_FILE = dataFilePath("users.json");
const QString BOOKS_FILE = dataFilePath("books.json");
const QString CARTS_FILE = dataFilePath("carts.json");
const QString REVIEWS_FILE = dataFilePath("reviews.json");

Server::Server(QObject *parent) : QTcpServer(parent) {}

// ================= Users =================

QJsonArray Server::loadUsers() {
    QFile file(USERS_FILE);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonArray();
    }
    QByteArray data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).array();
}

void Server::saveUsers(const QJsonArray& users) {
    QFile file(USERS_FILE);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(users).toJson());
        file.close();
    }
}

// ================= Books =================

QJsonArray Server::loadBooks() {
    QFile file(BOOKS_FILE);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "books.json not found or cannot be opened";
        return QJsonArray();
    }
    QByteArray data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).array();
}

// ================= Carts =================

QJsonArray Server::loadCarts() {
    QFile file(CARTS_FILE);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonArray();
    }
    QByteArray data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).array();
}

void Server::saveCarts(const QJsonArray& carts) {
    QFile file(CARTS_FILE);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(carts).toJson());
        file.close();
    }
}

// ================= Server lifecycle =================

void Server::startServer() {
    if (!this->listen(QHostAddress::Any, 1234)) {
        qDebug() << "Server could not start!";
    } else {
        qDebug() << "Server started on port 1234...";
    }
}

void Server::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(socketDescriptor);
    connect(socket, &QTcpSocket::readyRead, this, &Server::readyRead);
    connect(socket, &QTcpSocket::disconnected, this, &Server::disconnected);
    m_clients.append(socket);
    qDebug() << "New client connected:" << socketDescriptor;
}

void Server::disconnected() {
    qDebug() << "Client disconnected";
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        m_buffers.remove(socket);
        m_clients.removeAll(socket);
    }
}

void Server::sendResponse(QTcpSocket* socket, const QJsonObject& response) {
    if (!socket) return;
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n");
    socket->flush();
}

// وقتی نظر یا امتیاز کتابی تغییر می‌کند (ثبت/ویرایش/حذف)، لیست به‌روز نظرها و
// میانگین امتیاز را برای همه‌ی کلاینت‌های متصل می‌فرستد؛ هر کلاینتی که همان لحظه
// همین کتاب را باز نکرده باشد، این پیام را نادیده می‌گیرد (سمت BookDetailPage چک می‌شود)
void Server::broadcastReviewsUpdate(const QString &bookId) {
    QJsonArray reviews = loadReviews();
    QJsonArray bookReviews;
    for (const QJsonValue &val : reviews) {
        QJsonObject r = val.toObject();
        if (r["book_id"].toString() == bookId)
            bookReviews.append(r);
    }

    double avg;
    int count;
    calculateAverageRating(bookId, avg, count);

    QJsonObject response;
    response["type"] = "reviews_list";
    response["book_id"] = bookId;
    response["reviews"] = bookReviews;
    response["averageRating"] = avg;
    response["reviewCount"] = count;

    for (QTcpSocket *client : m_clients)
        sendResponse(client, response);
}

// ================= Dispatch =================

void Server::readyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    int newlineIndex;
    while ((newlineIndex = buffer.indexOf('\n')) != -1) {
        QByteArray line = buffer.left(newlineIndex);
        buffer.remove(0, newlineIndex + 1);
        if (line.trimmed().isEmpty()) continue;

        qDebug() << "SERVER RECEIVED:";
        qDebug().noquote() << line;
        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isNull()) continue;

        processMessage(socket, doc.object());
    }
}

void Server::processMessage(QTcpSocket* socket, const QJsonObject& json) {
    QString type = json.contains("type") ? json["type"].toString() : json["action"].toString();

    if (type == "login") {
        handleLogin(socket, json);
    } else if (type == "register") {
        handleRegister(socket, json);
    } else if (type == "get_users") {
        handleGetUsers(socket);
    } else if (type == "block_user") {
        handleBlockUser(socket, json);
    } else if (type == "delete_user") {
        handleDeleteUser(socket, json);
    } else if (type == "save_genres") {
        handleSaveGenres(socket, json);
    } else if (type == "get_security_question") {
        handleGetSecurityQuestion(socket, json);
    } else if (type == "verify_security_answer") {
        handleVerifySecurityAnswer(socket, json);
    } else if (type == "reset_password") {
        handleResetPassword(socket, json);
    } else if (type == "get_profile") {
        handleGetProfile(socket, json);
    } else if (type == "change_password") {
        handleChangePassword(socket, json);
    } else if (type == "get_books") {
        handleGetBooks(socket);
    } else if (type == "add_to_cart") {
        handleAddToCart(socket, json);
    } else if (type == "get_cart") {
        handleGetCart(socket, json);
    } else if (type == "remove_from_cart") {
        handleRemoveFromCart(socket, json);
    } else if (type == "checkout") {
        handleCheckout(socket, json);
    } else if (type == "get_library") {
        handleGetLibrary(socket, json);
    } else if (type == "get_purchase_history") {
        handleGetPurchaseHistory(socket, json);
    } else if (type == "search_books") {
        handleSearchBooks(socket, json);
    } else if (type == "post_review") {
        handlePostReview(socket, json);
    } else if (type == "edit_review") {
        handleEditReview(socket, json);
    } else if (type == "delete_review") {
        handleDeleteReview(socket, json);
    } else if (type == "get_reviews") {
        handleGetReviews(socket, json);
    } else if (type == "save_book") {
        handleSaveBook(socket, json);
    } else if (type == "unsave_book") {
        handleUnsaveBook(socket, json);
    } else if (type == "get_saved_books") {
        handleGetSavedBooks(socket, json);
    } else if (type == "create_shelf") {
        handleCreateShelf(socket, json);
    } else if (type == "delete_shelf") {
        handleDeleteShelf(socket, json);
    } else if (type == "add_book_to_shelf") {
        handleAddBookToShelf(socket, json);
    } else if (type == "remove_book_from_shelf") {
        handleRemoveBookFromShelf(socket, json);
    } else if (type == "get_shelves") {
        handleGetShelves(socket, json);
    } else if (type == "get_reading_progress") {
        handleGetReadingProgress(socket, json);
    } else if (type == "save_reading_progress") {
        handleSaveReadingProgress(socket, json);
    } else if (type == "publish_book") {
        handlePublishBook(socket, json);
    } else if (type == "update_book") {
        handleUpdateBook(socket, json);
    } else if (type == "deactivate_book") {
        handleSetBookActive(socket, json, false);
    } else if (type == "activate_book") {
        handleSetBookActive(socket, json, true);
    } else if (type == "get_publisher_books") {
        handleGetPublisherBooks(socket, json);
    } else if (type == "get_publisher_stats") {
        handleGetPublisherStats(socket, json);
    } else if (type == "admin_get_books") {
        handleAdminGetBooks(socket);
    } else if (type == "admin_update_book") {
        handleAdminUpdateBook(socket, json);
    } else if (type == "admin_delete_book") {
        handleAdminDeleteBook(socket, json);
    } else if (type == "admin_get_reviews") {
        handleAdminGetReviews(socket);
    } else if (type == "admin_delete_review") {
        handleAdminDeleteReview(socket, json);
    } else if (type == "toggle_favorite") {
        handleToggleFavorite(socket, json);
    }
    else if (type == "get_notifications") {
        handleGetNotifications(socket, json);
    }else if (type == "mark_notification_read") {
        handleMarkNotificationAsRead(socket, json);
    }

}

// ================= Auth handlers =================

void Server::handleLogin(QTcpSocket* socket, const QJsonObject& data) {

    QString username = data["username"].toString();
    QString passwordHash = data["password"].toString();
    QJsonArray users = loadUsers();

    QJsonObject response;
    response["type"] = "login_response";
    bool found = false;
    bool isBlocked = false;
    QString role;

    for (const QJsonValue& v : users) {
        QJsonObject userObj = v.toObject();
        if (userObj.value("username").toString() == username &&
            userObj.value("password").toString() == passwordHash) {

            found = true;
            QString status = userObj.value("status").toString("active");
            if (status == "blocked") {
                isBlocked = true;
            } else {
                role = userObj.value("role").toString();
            }

            bool firstLogin = userObj["firstLogin"].toBool(true);
            response["firstLogin"] = firstLogin;
            break;
        }
    }

    if (!found) {
        response["status"] = "error";
        response["message"] = "نام کاربری یا رمز عبور اشتباه است!";
    } else if (isBlocked) {
        response["status"] = "error";
        response["message"] = "حساب کاربری شما مسدود شده است و امکان ورود ندارید.";
    } else {
        response["status"] = "success";
        response["role"] = role;
        response["message"] = "ورود موفقیت‌آمیز";
        socket->setProperty("username", username);
    }

    sendResponse(socket, response);
}

void Server::handleRegister(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray users = loadUsers();

    QJsonObject response;
    response["type"] = "register_response";

    bool exists = false;
    for (const QJsonValue& v : users) {
        if (v.toObject().value("username").toString() == username) {
            exists = true;
            break;
        }
    }

    if (exists) {
        response["status"] = "error";
        response["message"] = "Username already exists!";
    } else {
        QJsonObject newUser = data;
        newUser["registration_date"] = QDateTime::currentDateTime().toString("yyyy/MM/dd");
        newUser["status"] = "active";
        newUser["firstLogin"] = true;
        newUser["favoriteGenres"] = QJsonArray();
        newUser["favoriteGenres"] = QJsonArray();
        newUser["personalLibrary"] = QJsonArray();


        users.append(newUser);
        saveUsers(users);

        response["status"] = "success";
        response["message"] = "Registration successful!";
    }

    sendResponse(socket, response);
}

void Server::handleGetUsers(QTcpSocket* socket) {
    QJsonArray users = loadUsers();
    QJsonArray usersList;

    for (const QJsonValue& v : users) {
        QJsonObject userObj = v.toObject();
        QJsonObject userSummary;

        userSummary["username"] = userObj.value("username").toString();
        userSummary["role"] = userObj.value("role").toString();
        userSummary["registration_date"] =
            userObj.value("registration_date").toString("نامشخص");
        userSummary["status"] =
            userObj.value("status").toString("active");

        QString role = userSummary["role"].toString().toLower();
        if (role == "customer") {
            userSummary["favoriteGenres"] =
                userObj.value("favoriteGenres").toArray();
        } else if (role == "publisher") {
            userSummary["publisher_name"] =
                userObj.value("publisher_name").toString("ثبت نشده");
        }

        usersList.append(userSummary);
    }

    QJsonObject response;
    response["type"] = "users_list";
    response["users"] = usersList;

    sendResponse(socket, response);
    socket->flush();
}

void Server::handleBlockUser(QTcpSocket* socket, const QJsonObject& data) {
    QString targetUser = data["username"].toString();
    QJsonArray users = loadUsers();
    bool success = false;

    for (int i = 0; i < users.size(); ++i) {
        QJsonObject userObj = users[i].toObject();
        if (userObj["username"].toString() == targetUser) {
            QString currentStatus = userObj.value("status").toString("active");
            userObj["status"] = (currentStatus == "active") ? "blocked" : "active";
            users[i] = userObj;
            success = true;
            break;
        }
    }

    if (success) saveUsers(users);

    QJsonObject response;
    response["type"] = "admin_action_response";
    response["success"] = success;
    response["message"] = success ? "وضعیت کاربر تغییر کرد" : "کاربر یافت نشد";
    sendResponse(socket, response);
}

void Server::handleDeleteUser(QTcpSocket* socket, const QJsonObject& data) {
    QString targetUser = data["username"].toString();
    QJsonArray users = loadUsers();
    bool success = false;

    for (int i = 0; i < users.size(); ++i) {
        if (users[i].toObject()["username"].toString() == targetUser) {
            users.removeAt(i);
            success = true;
            break;
        }
    }

    if (success) saveUsers(users);

    QJsonObject response;
    response["type"] = "admin_action_response";
    response["success"] = success;
    response["message"] = success ? "کاربر با موفقیت حذف شد" : "خطا در حذف کاربر";
    sendResponse(socket, response);
}

void Server::handleSaveGenres(QTcpSocket *socket, const QJsonObject &data)
{
    qDebug() << "handleSaveGenres entered";
    QString username = data["username"].toString();
    QJsonArray genres = data["genres"].toArray();

    QJsonArray users = loadUsers();
    bool success = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();

        if (user["username"].toString() == username) {
            user["favoriteGenres"] = genres;
            user["firstLogin"] = false;

            users[i] = user;
            success = true;
            break;
        }
    }

    if (success) {
        qDebug() << "Saving genres for:" << username;
        qDebug() << genres;
        saveUsers(users);
        qDebug() << "Users saved";
        qDebug() << QJsonDocument(users).toJson();
    }

    QJsonObject response;
    response["type"] = "save_genres_response";
    response["success"] = success;

    qDebug() << "Sending save_genres_response";
    sendResponse(socket, response);
    socket->flush();
    qDebug() << "save_genres_response sent";
}

void Server::handleGetSecurityQuestion(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "get_security_question_response";

    for (const QJsonValue& v : users) {
        QJsonObject u = v.toObject();
        if (u["username"].toString() == username) {
            response["success"] = true;
            response["question"] = u["securityQuestion"].toString();
            sendResponse(socket, response);
            return;
        }
    }
    response["success"] = false;
    response["message"] = "کاربر یافت نشد";
    sendResponse(socket, response);
}

void Server::handleVerifySecurityAnswer(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString answer = data["answer"].toString();
    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "verify_security_answer_response";

    for (const QJsonValue& v : users) {
        QJsonObject u = v.toObject();
        if (u["username"].toString() == username) {
            response["success"] = (u["securityAnswer"].toString() == answer);
            sendResponse(socket, response);
            return;
        }
    }
    response["success"] = false;
    sendResponse(socket, response);
}

void Server::handleResetPassword(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString newPasswordHash = data["newPassword"].toString();
    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "reset_password_response";
    bool success = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject u = users[i].toObject();
        if (u["username"].toString() == username) {
            u["password"] = newPasswordHash;
            users[i] = u;
            success = true;
            break;
        }
    }
    if (success) saveUsers(users);
    response["success"] = success;
    sendResponse(socket, response);
}

void Server::handleGetProfile(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray users = loadUsers();

    QJsonObject response;
    response["type"] = "profile_response";

    for (const QJsonValue& v : users) {
        QJsonObject u = v.toObject();
        if (u["username"].toString() == username) {
            response["success"] = true;
            response["username"] = username;
            response["role"] = u.value("role").toString();
            response["registration_date"] = u.value("registration_date").toString("نامشخص");
            response["favoriteGenres"] = u.value("favoriteGenres").toArray();
            sendResponse(socket, response);
            return;
        }
    }
    response["success"] = false;
    response["message"] = "کاربر یافت نشد";
    sendResponse(socket, response);
}

void Server::handleChangePassword(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString oldPasswordHash = data["oldPassword"].toString();
    QString newPasswordHash = data["newPassword"].toString();

    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "change_password_response";

    for (int i = 0; i < users.size(); i++) {
        QJsonObject u = users[i].toObject();
        if (u["username"].toString() == username) {
            if (u.value("password").toString() != oldPasswordHash) {
                response["success"] = false;
                response["message"] = "رمز عبور فعلی اشتباه است";
                sendResponse(socket, response);
                return;
            }
            u["password"] = newPasswordHash;
            users[i] = u;
            saveUsers(users);
            response["success"] = true;
            response["message"] = "رمز عبور با موفقیت تغییر کرد";
            sendResponse(socket, response);
            return;
        }
    }
    response["success"] = false;
    response["message"] = "کاربر یافت نشد";
    sendResponse(socket, response);
}

// ================= Book handlers =================

void Server::handleGetBooks(QTcpSocket* socket) {
    QJsonArray allBooks = loadBooks();
    QJsonArray books;
    for (const QJsonValue &v : allBooks) {
        QJsonObject book = v.toObject();
        // کتاب‌های غیرفعال‌شده توسط ناشر از فروشگاه/جستجو حذف می‌شوند؛ کاربرانی که
        // قبلاً خریده‌اند همچنان از طریق کتابخانه شخصی (handleGetLibrary) دسترسی دارند
        if (book.value("isActive").toBool(true))
            books.append(book);
    }

    QJsonObject response;
    response["type"] = "books_list";
    response["books"] = books;

    sendResponse(socket, response);
    socket->flush();
}

// قیمت واقعی یک کتاب بعد از اعمال تخفیف درصدی که ناشر روی آن گذاشته
double Server::effectivePrice(const QJsonObject &book) {
    double price = book.value("price").toDouble();
    double discountPercent = book.value("discountPercent").toDouble(0);
    if (discountPercent <= 0) return price;
    if (discountPercent >= 100) return 0;
    return price * (1.0 - discountPercent / 100.0);
}

// ================= Cart handlers =================

void Server::handleAddToCart(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();
    int quantity = data.value("quantity").toInt(1);

    QJsonArray carts = loadCarts();
    bool found = false;

    for (int i = 0; i < carts.size(); ++i) {
        QJsonObject item = carts[i].toObject();
        if (item["username"].toString() == username &&
            item["book_id"].toString() == bookId) {
            item["quantity"] = item["quantity"].toInt(1) + quantity;
            carts[i] = item;
            found = true;
            break;
        }
    }

    if (!found) {
        QJsonObject newItem;
        newItem["username"] = username;
        newItem["book_id"] = bookId;
        newItem["quantity"] = quantity;
        carts.append(newItem);
    }

    saveCarts(carts);

    QJsonObject response;
    response["type"] = "add_to_cart_response";
    response["success"] = true;
    sendResponse(socket, response);
    socket->flush();
}

void Server::handleGetCart(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray carts = loadCarts();
    QJsonArray books = loadBooks();

    QJsonArray cartItems;
    double originalTotal = 0;
    double discount = 0;
    int itemCount = 0;
    for (const QJsonValue& v : carts) {
        QJsonObject item = v.toObject();
        if (item["username"].toString() != username) continue;

        QString bookId = item["book_id"].toString();
        for (const QJsonValue& bv : books) {
            QJsonObject book = bv.toObject();
            if (book["id"].toString() == bookId) {
                int quantity = item["quantity"].toInt(1);
                double rawPrice = book.value("price").toDouble();
                double price = effectivePrice(book); // قیمت بعد از تخفیف ناشر روی این کتاب

                QJsonObject cartEntry;
                cartEntry["book_id"] = bookId;
                cartEntry["title"] = book["title"].toString();
                cartEntry["price"] = price;
                cartEntry["quantity"] = quantity;
                cartItems.append(cartEntry);

                originalTotal += rawPrice * quantity;
                discount += (rawPrice - price) * quantity;
                itemCount += quantity;
                break;
            }
        }
    }

    QJsonObject response;
    response["type"] = "cart_response";
    response["items"] = cartItems;
    response["itemCount"] = itemCount;
    response["total"] = originalTotal;
    response["discountAmount"] = discount;
    response["finalAmount"] = originalTotal - discount;
    sendResponse(socket, response);
    socket->flush();
}

void Server::handleRemoveFromCart(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray carts = loadCarts();
    bool success = false;

    for (int i = 0; i < carts.size(); ++i) {
        QJsonObject item = carts[i].toObject();
        if (item["username"].toString() == username &&
            item["book_id"].toString() == bookId) {
            carts.removeAt(i);
            success = true;
            break;
        }
    }

    if (success) saveCarts(carts);

    QJsonObject response;
    response["type"] = "remove_from_cart_response";
    response["success"] = success;
    sendResponse(socket, response);
    socket->flush();
}

QJsonArray Server::loadPurchaseHistory()
{
    QFile file(dataFilePath("purchase_history.json"));
    if (!file.open(QIODevice::ReadOnly))
        return QJsonArray();
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc.array();
}

void Server::savePurchaseHistory(const QJsonArray &history)
{
    QFile file(dataFilePath("purchase_history.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(history).toJson());
        file.close();
    }
}

void Server::addBooksToLibrary(const QString &username, const QJsonArray &purchasedItems)
{
    QJsonArray users = loadUsers();
    for (int i = 0; i < users.size(); ++i) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonArray library = user.value("personalLibrary").toArray();
            for (const QJsonValue &v : purchasedItems) {
                QJsonObject item = v.toObject();
                library.append(item["book_id"].toString());
            }
            user["personalLibrary"] = library;
            users[i] = user;
            break;
        }
    }
    saveUsers(users);
}

void Server::handleCheckout(QTcpSocket* socket, const QJsonObject& data)
{
    QString username = data["username"].toString();
    QJsonArray carts = loadCarts();
    QJsonArray books = loadBooks();

    QJsonArray userItems;
    QJsonArray remainingCarts;
    double total = 0;
    bool stockError = false;
    QString stockErrorBook;

    // جدا کردن آیتم‌های این کاربر
    for (const QJsonValue &v : carts) {
        QJsonObject item = v.toObject();
        if (item["username"].toString() == username) {
            QString bookId = item["book_id"].toString();
            int qty = item["quantity"].toInt(1);

            // بررسی موجودی کتاب
            for (const QJsonValue &bv : books) {
                QJsonObject book = bv.toObject();
                if (book["id"].toString() == bookId) {
                    int stock = book["stock"].toInt();
                    if (stock < qty) {
                        stockError = true;
                        stockErrorBook = book["title"].toString();
                    }
                    break;
                }
            }
            userItems.append(item);
        } else {
            remainingCarts.append(item);
        }
    }

    // ۱) بررسی سبد خالی
    if (userItems.isEmpty()) {
        QJsonObject response;
        response["type"] = "checkout_response";
        response["success"] = false;
        response["message"] = "سبد خرید خالی است";
        response["total"] = 0;
        response["discountAmount"] = 0;
        response["finalAmount"] = 0;
        sendResponse(socket, response);
        return;
    }

    // ۲) بررسی موجودی
    if (stockError) {
        QJsonObject response;
        response["type"] = "checkout_response";
        response["success"] = false;
        response["message"] = "موجودی کافی نیست: " + stockErrorBook;
        sendResponse(socket, response);
        return;
    }

    // ۳) محاسبه مبلغ کل (قبل از تخفیف)، مجموع تخفیفِ ناشرها و کاهش موجودی
    int itemCount = 0;
    double discount = 0;
    for (const QJsonValue &v : userItems) {
        QJsonObject item = v.toObject();
        QString bookId = item["book_id"].toString();
        int qty = item["quantity"].toInt(1);
        itemCount += qty;

        for (int i = 0; i < books.size(); ++i) {
            QJsonObject book = books[i].toObject();
            if (book["id"].toString() == bookId) {
                double rawPrice = book.value("price").toDouble();
                double price = effectivePrice(book);
                total += rawPrice * qty;
                discount += (rawPrice - price) * qty;
                book["stock"] = book["stock"].toInt() - qty;
                books[i] = book;
                break;
            }
        }
    }

    saveBooks(books);

    // ۴) مبلغ نهایی پس از کسرِ تخفیف‌های فعال روی کتاب‌ها
    double finalAmount = total - discount;

    // ۵) ذخیره سبد باقی‌مانده
    saveCarts(remainingCarts);

    // ۶) ثبت تاریخچه خرید
    QJsonArray history = loadPurchaseHistory();
    QJsonObject record;
    record["username"] = username;
    record["items"] = userItems;
    record["total"] = total;
    record["discountAmount"] = discount;
    record["finalAmount"] = finalAmount;
    record["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    history.append(record);
    savePurchaseHistory(history);

    // ۷) انتقال به کتابخانه شخصی
    addBooksToLibrary(username, userItems);

    // ۸) پاسخ نهایی
    QJsonObject response;
    response["type"] = "checkout_response";
    response["success"] = true;
    response["total"] = total;
    response["discountAmount"] = discount;
    response["finalAmount"] = finalAmount;
    response["message"] = "خرید با موفقیت انجام شد";
    sendResponse(socket, response);


    // ارسال نوتیفیکیشن به ناشر برای هر کتاب خریداری‌شده
    for (const QJsonValue &itemVal : userItems)
        // ارسال نوتیفیکیشن به ناشر برای هر کتاب خریداری‌شده
        for (const QJsonValue &itemVal : userItems) {
            QJsonObject item = itemVal.toObject();
            // تبدیل مطمئن id به رشته، چه عدد باشد چه string
            QString bId = item["book_id"].toVariant().toString();

            for (const QJsonValue &bv : books) {
                QJsonObject bObj = bv.toObject();
                QString currentBookId = bObj["id"].toVariant().toString();

                if (currentBookId == bId) {
                    QString bTitle = bObj["title"].toString();

                    // خواندن نام ناشر با حساسیست‌زدایی نسبت به حروف کوچک/بزرگ و فاصله‌ها
                    QString publisherName = bObj["publisher"].toString().trimmed();
                    if (publisherName.isEmpty()) {
                        publisherName = bObj["author"].toString().trimmed();
                    }

                    if (!publisherName.isEmpty()) {
                        sendNotificationToPublisher(
                            publisherName,
                            "فروش جدید 💰",
                            QString("کتاب «%1» توسط کاربر %2 خریداری شد.").arg(bTitle, username)
                            );
                    }
                    break;
                }
            }
        }


}
void Server::saveBooks(const QJsonArray &books) {
    QFile file(BOOKS_FILE);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(books).toJson());
        file.close();
    }
}
//دریافت کتابخانه شخصی کاربر
void Server::handleGetLibrary(QTcpSocket* socket, const QJsonObject& data)
{
    QString username = data["username"].toString();
    QJsonArray users = loadUsers();
    QJsonArray library;
    QJsonArray favoritesList; // لیست کتاب‌های کامل علاقه‌مندی

    for (const QJsonValue &v : users) {
        QJsonObject user = v.toObject();
        if (user["username"].toString() == username) {
            QJsonArray allBooks = loadBooks();

            // ۱. تبدیل شناسه‌های کتابخانه شخصی به اطلاعات کامل کتاب
            QJsonArray bookIds = user.value("personalLibrary").toArray();
            for (const QJsonValue &idVal : bookIds) {
                QString bookId = idVal.toString();
                for (const QJsonValue &bv : allBooks) {
                    QJsonObject book = bv.toObject();
                    if (book["id"].toString() == bookId) {
                        library.append(book);
                        break;
                    }
                }
            }

            // ۲. تبدیل شناسه‌های علاقه‌مندی‌ها به اطلاعات کامل کتاب
            QJsonArray favIds = user.value("favorites").toArray();
            for (const QJsonValue &favVal : favIds) {
                QString favId = favVal.toString();
                for (const QJsonValue &bv : allBooks) {
                    QJsonObject book = bv.toObject();
                    if (book["id"].toString() == favId) {
                        favoritesList.append(book);
                        break;
                    }
                }
            }

            break;
        }
    }

    // ارسال پاسخ به کلاینت شامل کتابخانه شخصی و لیست علاقه‌مندی‌ها
    QJsonObject response;
    response["type"] = "library_response";
    response["items"] = library;
    response["favorites"] = favoritesList; // اضافه شدن آرایه کامل علاقه‌مندی‌ها
    sendResponse(socket, response);
}

void Server::handleGetPurchaseHistory(QTcpSocket* socket, const QJsonObject& data)
{
    QString username = data["username"].toString();
    QJsonArray allHistory = loadPurchaseHistory();
    QJsonArray userHistory;

    for (const QJsonValue &v : allHistory) {
        QJsonObject record = v.toObject();
        if (record["username"].toString() == username) {
            userHistory.append(record);
        }
    }

    QJsonObject response;
    response["type"] = "purchase_history_response";
    response["items"] = userHistory;
    sendResponse(socket, response);
}

// ================= Search =================

void Server::handleSearchBooks(QTcpSocket* socket, const QJsonObject& data) {
    QString query = data["query"].toString().toLower().trimmed();
    QString field = data["field"].toString();

    QJsonArray allBooks = loadBooks();
    QJsonArray result;
    for (const QJsonValue &val : allBooks) {
        QJsonObject book = val.toObject();
        if (!book.value("isActive").toBool(true)) continue;
        if (book[field].toString().toLower().contains(query))
            result.append(book);
    }
    result = enrichBooksWithRatings(result);

    QJsonObject response;
    response["type"] = "search_result";
    response["books"] = result;
    sendResponse(socket, response);
    socket->flush();
}

// ================= Reviews / Ratings =================

QJsonArray Server::loadReviews() {
    QFile file(REVIEWS_FILE);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonArray();
    }
    QByteArray data = file.readAll();
    file.close();
    return QJsonDocument::fromJson(data).array();
}

void Server::saveReviews(const QJsonArray &reviews) {
    QFile file(REVIEWS_FILE);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(reviews).toJson());
        file.close();
    }
}

void Server::calculateAverageRating(const QString &bookId, double &avgOut, int &countOut) {
    QJsonArray reviews = loadReviews();
    double sum = 0;
    int count = 0;
    for (const QJsonValue &val : reviews) {
        QJsonObject review = val.toObject();
        if (review["book_id"].toString() == bookId) {
            sum += review["rating"].toDouble();
            count++;
        }
    }
    countOut = count;
    avgOut = (count > 0) ? (sum / count) : 0.0;
}

QJsonArray Server::enrichBooksWithRatings(QJsonArray books) {
    QJsonArray result;
    for (const QJsonValue &val : books) {
        QJsonObject book = val.toObject();
        double avg;
        int count;
        calculateAverageRating(book["id"].toString(), avg, count);
        book["averageRating"] = avg;
        book["reviewCount"] = count;
        result.append(book);
    }
    return result;
}

void Server::handlePostReview(QTcpSocket* socket, const QJsonObject& data) {
    QString bookId = data["book_id"].toString();
    QString username = data["username"].toString();
    int rating = data["rating"].toInt();
    QString comment = data["comment"].toString();

    QJsonObject response;
    response["type"] = "review_posted";

    if (bookId.isEmpty() || username.isEmpty() || rating < 1 || rating > 5) {
        response["success"] = false;
        response["message"] = "اطلاعات نظر نامعتبر است";
        sendResponse(socket, response);
        socket->flush();
        return;
    }

    QJsonArray reviews = loadReviews();
    for (const QJsonValue &val : reviews) {
        QJsonObject r = val.toObject();
        if (r["book_id"].toString() == bookId && r["username"].toString() == username) {
            response["success"] = false;
            response["message"] = "شما قبلاً برای این کتاب نظر ثبت کرده‌اید";
            sendResponse(socket, response);
            socket->flush();
            return;
        }
    }


    QJsonObject newReview;
    newReview["review_id"] = "r" + QString::number(QDateTime::currentMSecsSinceEpoch());
    newReview["book_id"] = bookId;
    newReview["username"] = username;
    newReview["rating"] = rating;
    newReview["comment"] = comment;
    newReview["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    reviews.append(newReview);
    saveReviews(reviews);

    response["success"] = true;
    response["message"] = "نظر شما ثبت شد";
    sendResponse(socket, response);
    socket->flush();

    // --- اضافه شده برای ارسال نوتیفیکیشن به ناشر ---
    QJsonArray books = loadBooks();
    for (const QJsonValue &v : books) {
        QJsonObject b = v.toObject();
        if (b["id"].toString() == bookId) {
            QString pub = b["publisher"].toString();
            if (pub.isEmpty()) pub = b["author"].toString();
            if (!pub.isEmpty()) {
                sendNotificationToPublisher(
                    pub,
                    "دیدگاه جدید 💬",
                    QString("کاربر «%1» برای کتاب «%2» نظر جدیدی ثبت کرد.").arg(username, b["title"].toString())
                    );
            }
            break;
        }
    }
    // ------------------------------------------------

    broadcastReviewsUpdate(bookId);
}

void Server::handleEditReview(QTcpSocket* socket, const QJsonObject& data) {
    QString reviewId = data["review_id"].toString();
    QString username = data["username"].toString();

    QJsonArray reviews = loadReviews();
    QJsonObject response;
    response["type"] = "review_edited";
    bool found = false;
    QString bookId;

    for (int i = 0; i < reviews.size(); i++) {
        QJsonObject r = reviews[i].toObject();
        if (r["review_id"].toString() == reviewId && r["username"].toString() == username) {
            if (data.contains("rating")) r["rating"] = data["rating"].toInt();
            if (data.contains("comment")) r["comment"] = data["comment"].toString();
            r["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            bookId = r["book_id"].toString();
            reviews[i] = r;
            found = true;
            break;
        }
    }

    if (found) {
        saveReviews(reviews);
        response["success"] = true;
    } else {
        response["success"] = false;
        response["message"] = "نظر یافت نشد یا مجاز به ویرایش نیستید";
    }
    sendResponse(socket, response);
    socket->flush();

    if (found) broadcastReviewsUpdate(bookId);
}

void Server::handleDeleteReview(QTcpSocket* socket, const QJsonObject& data) {
    QString reviewId = data["review_id"].toString();
    QString username = data["username"].toString();

    QJsonArray reviews = loadReviews();
    QJsonArray updated;
    bool found = false;
    QString bookId;

    for (const QJsonValue &val : reviews) {
        QJsonObject r = val.toObject();
        if (r["review_id"].toString() == reviewId && r["username"].toString() == username) {
            bookId = r["book_id"].toString();
            found = true;
            continue;
        }
        updated.append(r);
    }

    QJsonObject response;
    response["type"] = "review_deleted";
    if (found) {
        saveReviews(updated);
        response["success"] = true;
    } else {
        response["success"] = false;
        response["message"] = "نظر یافت نشد یا مجاز به حذف نیستید";
    }
    sendResponse(socket, response);
    socket->flush();

    if (found) broadcastReviewsUpdate(bookId);
}

void Server::handleGetReviews(QTcpSocket* socket, const QJsonObject& data) {
    QString bookId = data["book_id"].toString();
    QJsonArray reviews = loadReviews();
    QJsonArray bookReviews;

    for (const QJsonValue &val : reviews) {
        QJsonObject r = val.toObject();
        if (r["book_id"].toString() == bookId)
            bookReviews.append(r);
    }

    double avg;
    int count;
    calculateAverageRating(bookId, avg, count);

    QJsonObject response;
    response["type"] = "reviews_list";
    response["book_id"] = bookId;
    response["reviews"] = bookReviews;
    response["averageRating"] = avg;
    response["reviewCount"] = count;

    sendResponse(socket, response);
    socket->flush();

}

// ================= Saved books (wishlist) =================

void Server::handleSaveBook(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "save_book_response";
    bool found = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonArray saved = user.value("savedBooks").toArray();
            bool already = false;
            for (const QJsonValue &v : saved)
                if (v.toString() == bookId) { already = true; break; }

            if (!already) {
                saved.append(bookId);
                user["savedBooks"] = saved;
                users[i] = user;
                saveUsers(users);
            }
            found = true;
            response["success"] = true;
            response["message"] = already ? "این کتاب قبلاً ذخیره شده بود" : "کتاب برای مطالعه بعدی ذخیره شد";
            break;
        }
    }

    if (!found) {
        response["success"] = false;
        response["message"] = "کاربر یافت نشد";
    }
    sendResponse(socket, response);
    socket->flush();
}

void Server::handleUnsaveBook(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "unsave_book_response";
    bool found = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonArray saved = user.value("savedBooks").toArray();
            QJsonArray updated;
            for (const QJsonValue &v : saved)
                if (v.toString() != bookId) updated.append(v);

            user["savedBooks"] = updated;
            users[i] = user;
            saveUsers(users);
            found = true;
            response["success"] = true;
            response["message"] = "از لیست ذخیره‌شده حذف شد";
            break;
        }
    }

    if (!found) {
        response["success"] = false;
        response["message"] = "کاربر یافت نشد";
    }
    sendResponse(socket, response);
    socket->flush();
}

void Server::handleGetSavedBooks(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray users = loadUsers();
    QJsonArray items;

    for (const QJsonValue &v : users) {
        QJsonObject user = v.toObject();
        if (user["username"].toString() == username) {
            QJsonArray bookIds = user.value("savedBooks").toArray();
            QJsonArray allBooks = loadBooks();

            for (const QJsonValue &idVal : bookIds) {
                QString bookId = idVal.toString();
                for (const QJsonValue &bv : allBooks) {
                    QJsonObject book = bv.toObject();
                    if (book["id"].toString() == bookId) {
                        items.append(book);
                        break;
                    }
                }
            }
            break;
        }
    }

    QJsonObject response;
    response["type"] = "saved_books_response";
    response["items"] = items;
    sendResponse(socket, response);
    socket->flush();
}

// ================= قفسه‌ها و دسته‌بندی‌های شخصی =================

// پاسخ مشترک برای هر عملیات روی قفسه‌ها: لیست کامل و به‌روز قفسه‌ها (با جزئیات کتاب‌های
// هرکدوم) به همراه نتیجه‌ی همان عملیات، تا کلاینت مجبور نباشد برای هر عملیات یک
// درخواست جدا برای دریافت لیست ارسال کند
void Server::sendShelvesResponse(QTcpSocket* socket, const QString &username, bool success, const QString &message) {
    QJsonArray users = loadUsers();
    QJsonArray allBooks = loadBooks();
    QJsonArray shelvesOut;

    for (const QJsonValue &v : users) {
        QJsonObject user = v.toObject();
        if (user["username"].toString() != username) continue;

        QJsonArray shelves = user.value("shelves").toArray();
        for (const QJsonValue &sv : shelves) {
            QJsonObject shelf = sv.toObject();
            QJsonArray bookIds = shelf.value("bookIds").toArray();
            QJsonArray books;

            for (const QJsonValue &idVal : bookIds) {
                QString bookId = idVal.toString();
                for (const QJsonValue &bv : allBooks) {
                    QJsonObject book = bv.toObject();
                    if (book["id"].toString() == bookId) {
                        books.append(book);
                        break;
                    }
                }
            }

            QJsonObject shelfOut;
            shelfOut["id"] = shelf.value("id").toString();
            shelfOut["name"] = shelf.value("name").toString();
            shelfOut["books"] = books;
            shelvesOut.append(shelfOut);
        }
        break;
    }

    QJsonObject response;
    response["type"] = "shelves_response";
    response["success"] = success;
    response["message"] = message;
    response["shelves"] = shelvesOut;
    sendResponse(socket, response);
    socket->flush();
}

void Server::handleGetShelves(QTcpSocket* socket, const QJsonObject& data) {
    sendShelvesResponse(socket, data["username"].toString(), true, "");
}

void Server::handleCreateShelf(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString name = data["name"].toString().trimmed();

    if (name.isEmpty()) {
        sendShelvesResponse(socket, username, false, "نام قفسه نمی‌تواند خالی باشد");
        return;
    }

    QJsonArray users = loadUsers();
    bool found = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonArray shelves = user.value("shelves").toArray();

            QJsonObject newShelf;
            newShelf["id"] = "sh" + QString::number(QDateTime::currentMSecsSinceEpoch());
            newShelf["name"] = name;
            newShelf["bookIds"] = QJsonArray();
            shelves.append(newShelf);

            user["shelves"] = shelves;
            users[i] = user;
            saveUsers(users);
            found = true;
            break;
        }
    }

    sendShelvesResponse(socket, username, found, found ? "قفسه ایجاد شد" : "کاربر یافت نشد");
}

void Server::handleDeleteShelf(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString shelfId = data["shelf_id"].toString();

    QJsonArray users = loadUsers();
    bool found = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonArray shelves = user.value("shelves").toArray();
            QJsonArray updated;
            for (const QJsonValue &v : shelves) {
                if (v.toObject().value("id").toString() != shelfId) updated.append(v);
                else found = true;
            }
            user["shelves"] = updated;
            users[i] = user;
            saveUsers(users);
            break;
        }
    }

    sendShelvesResponse(socket, username, found, found ? "قفسه حذف شد" : "قفسه یافت نشد");
}

void Server::handleAddBookToShelf(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString shelfId = data["shelf_id"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray users = loadUsers();
    bool found = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() != username) continue;

        QJsonArray shelves = user.value("shelves").toArray();
        for (int j = 0; j < shelves.size(); j++) {
            QJsonObject shelf = shelves[j].toObject();
            if (shelf.value("id").toString() != shelfId) continue;

            QJsonArray bookIds = shelf.value("bookIds").toArray();
            bool already = false;
            for (const QJsonValue &v : bookIds)
                if (v.toString() == bookId) { already = true; break; }

            if (!already) bookIds.append(bookId);
            shelf["bookIds"] = bookIds;
            shelves[j] = shelf;
            found = true;
            break;
        }

        if (found) {
            user["shelves"] = shelves;
            users[i] = user;
            saveUsers(users);
        }
        break;
    }

    sendShelvesResponse(socket, username, found, found ? "کتاب به قفسه اضافه شد" : "قفسه یافت نشد");
}

void Server::handleRemoveBookFromShelf(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString shelfId = data["shelf_id"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray users = loadUsers();
    bool found = false;

    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() != username) continue;

        QJsonArray shelves = user.value("shelves").toArray();
        for (int j = 0; j < shelves.size(); j++) {
            QJsonObject shelf = shelves[j].toObject();
            if (shelf.value("id").toString() != shelfId) continue;

            QJsonArray bookIds = shelf.value("bookIds").toArray();
            QJsonArray updated;
            for (const QJsonValue &v : bookIds)
                if (v.toString() != bookId) updated.append(v);

            shelf["bookIds"] = updated;
            shelves[j] = shelf;
            found = true;
            break;
        }

        if (found) {
            user["shelves"] = shelves;
            users[i] = user;
            saveUsers(users);
        }
        break;
    }

    sendShelvesResponse(socket, username, found, found ? "کتاب از قفسه حذف شد" : "قفسه یافت نشد");
}

// ================= پیشرفت مطالعه =================

void Server::handleGetReadingProgress(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray users = loadUsers();
    int page = 0;

    for (const QJsonValue &v : users) {
        QJsonObject user = v.toObject();
        if (user["username"].toString() == username) {
            QJsonObject progress = user.value("readingProgress").toObject();
            page = progress.value(bookId).toInt(0);
            break;
        }
    }

    QJsonObject response;
    response["type"] = "reading_progress_response";
    response["book_id"] = bookId;
    response["page"] = page;
    sendResponse(socket, response);
    socket->flush();
}

void Server::handleSaveReadingProgress(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();
    int page = data["page"].toInt();

    QJsonArray users = loadUsers();
    for (int i = 0; i < users.size(); i++) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonObject progress = user.value("readingProgress").toObject();
            progress[bookId] = page;
            user["readingProgress"] = progress;
            users[i] = user;
            saveUsers(users);
            break;
        }
    }
    // نیازی به پاسخ نیست؛ ذخیره‌ی پیشرفت مطالعه در پس‌زمینه انجام می‌شود
}

// ================= پنل ناشر =================

void Server::handlePublishBook(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString title = data["title"].toString().trimmed();
    QString author = data["author"].toString().trimmed();
    QString genre = data["genre"].toString().trimmed();

    QJsonObject response;
    response["type"] = "publish_book_response";

    if (username.isEmpty() || title.isEmpty() || author.isEmpty()) {
        response["success"] = false;
        response["message"] = "اطلاعات کتاب ناقص است";
        sendResponse(socket, response);
        return;
    }

    QJsonArray books = loadBooks();

    QJsonObject newBook;
    newBook["id"] = "b" + QString::number(QDateTime::currentMSecsSinceEpoch());
    newBook["title"] = title;
    newBook["author"] = author;
    newBook["genre"] = data["genre"].toString();
    newBook["description"] = data["description"].toString();
    double price = data["price"].toDouble();
    newBook["price"] = price;
    newBook["discountPercent"] = data.value("discountPercent").toDouble(0);
    newBook["coverImage"] = data.value("coverImage").toString();
    newBook["fileURL"] = data.value("fileURL").toString();
    newBook["publisher"] = username;
    newBook["publisherUsername"] = username;
    newBook["isActive"] = true;
    newBook["isNew"] = true;
    newBook["isBestseller"] = false;
    newBook["isPopular"] = false;
    newBook["isFree"] = price <= 0;
    newBook["averageRating"] = 0.0;
    // کتاب دیجیتال هست، محدودیت موجودی فیزیکی معنی ندارد
    newBook["stock"] = 999999;

    books.append(newBook);
    saveBooks(books);

    response["success"] = true;
    response["message"] = "کتاب با موفقیت منتشر شد";
    response["book"] = newBook;
    sendResponse(socket, response);

    sendGenreNotification(genre, title, author);
}

void Server::handleUpdateBook(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray books = loadBooks();
    QJsonObject response;
    response["type"] = "update_book_response";

    for (int i = 0; i < books.size(); i++) {
        QJsonObject book = books[i].toObject();
        if (book["id"].toString() != bookId) continue;

        if (book.value("publisherUsername").toString() != username) {
            response["success"] = false;
            response["message"] = "شما اجازه‌ی ویرایش این کتاب را ندارید";
            sendResponse(socket, response);
            return;
        }

        double oldDiscount = book.value("discountPercent").toDouble(0);

        if (data.contains("title")) book["title"] = data["title"].toString();
        if (data.contains("author")) book["author"] = data["author"].toString();
        if (data.contains("genre")) book["genre"] = data["genre"].toString();
        if (data.contains("description")) book["description"] = data["description"].toString();
        if (data.contains("price")) book["price"] = data["price"].toDouble();
        if (data.contains("discountPercent")) book["discountPercent"] = data["discountPercent"].toDouble();
        if (data.contains("coverImage")) book["coverImage"] = data["coverImage"].toString();
        if (data.contains("fileURL")) book["fileURL"] = data["fileURL"].toString();
        book["isFree"] = book.value("price").toDouble() <= 0;

        books[i] = book;
        saveBooks(books);
        response["success"] = true;
        response["message"] = "اطلاعات کتاب به‌روزرسانی شد";
        sendResponse(socket, response);

        double newDiscount = book.value("discountPercent").toDouble(0);
        if (newDiscount > 0 && newDiscount != oldDiscount) {
            double originalPrice = book.value("price").toDouble(0);
            double discountedPrice = originalPrice * (1.0 - newDiscount / 100.0);

            sendDiscountNotification(bookId, book.value("title").toString(), newDiscount, discountedPrice);
        }

        return;
    }

    response["success"] = false;
    response["message"] = "کتاب یافت نشد";
    sendResponse(socket, response);
}

void Server::handleSetBookActive(QTcpSocket* socket, const QJsonObject& data, bool active) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray books = loadBooks();
    QJsonObject response;
    response["type"] = active ? "activate_book_response" : "deactivate_book_response";

    for (int i = 0; i < books.size(); i++) {
        QJsonObject book = books[i].toObject();
        if (book["id"].toString() != bookId) continue;

        if (book.value("publisherUsername").toString() != username) {
            response["success"] = false;
            response["message"] = "شما اجازه‌ی مدیریت این کتاب را ندارید";
            sendResponse(socket, response);
            return;
        }

        book["isActive"] = active;
        books[i] = book;
        saveBooks(books);

        response["success"] = true;
        response["message"] = active
                                  ? "کتاب دوباره فعال شد"
                                  : "کتاب از فروشگاه حذف شد (کاربرانی که قبلاً خریده‌اند همچنان دسترسی دارند)";
        sendResponse(socket, response);
        return;
    }

    response["success"] = false;
    response["message"] = "کتاب یافت نشد";
    sendResponse(socket, response);
}

void Server::handleGetPublisherBooks(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray allBooks = enrichBooksWithRatings(loadBooks());
    QJsonArray myBooks;

    for (const QJsonValue &v : allBooks) {
        QJsonObject book = v.toObject();
        if (book.value("publisherUsername").toString() == username)
            myBooks.append(book);
    }

    QJsonObject response;
    response["type"] = "publisher_books_response";
    response["books"] = myBooks;
    sendResponse(socket, response);
}

void Server::handleGetPublisherStats(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QJsonArray allBooks = loadBooks();
    QJsonArray history = loadPurchaseHistory();

    // فقط کتاب‌های همین ناشر
    QHash<QString, QJsonObject> myBooks; // bookId -> book
    for (const QJsonValue &v : allBooks) {
        QJsonObject book = v.toObject();
        if (book.value("publisherUsername").toString() == username)
            myBooks[book["id"].toString()] = book;
    }

    QHash<QString, int> salesCount;
    double totalRevenue = 0;

    for (const QJsonValue &hv : history) {
        QJsonObject record = hv.toObject();
        QJsonArray items = record.value("items").toArray();
        for (const QJsonValue &iv : items) {
            QJsonObject item = iv.toObject();
            QString bookId = item.value("book_id").toString();
            if (!myBooks.contains(bookId)) continue;

            int qty = item.value("quantity").toInt(1);
            salesCount[bookId] += qty;
            totalRevenue += effectivePrice(myBooks.value(bookId)) * qty;
        }
    }

    QJsonArray bookStats;
    for (auto it = myBooks.constBegin(); it != myBooks.constEnd(); ++it) {
        QJsonObject book = it.value();
        double avg; int count;
        calculateAverageRating(it.key(), avg, count);

        QJsonObject stat;
        stat["id"] = it.key();
        stat["title"] = book["title"].toString();
        stat["averageRating"] = avg;
        stat["reviewCount"] = count;
        stat["salesCount"] = salesCount.value(it.key(), 0);
        bookStats.append(stat);
    }

    QList<QJsonObject> sorted;
    for (const QJsonValue &v : bookStats) sorted.append(v.toObject());
    std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a["salesCount"].toInt() > b["salesCount"].toInt();
    });

    QJsonArray bestSellers, worstSellers;
    for (int i = 0; i < sorted.size() && i < 5; i++) bestSellers.append(sorted[i]);
    for (int i = sorted.size() - 1; i >= 0 && sorted.size() - i <= 5; i--) worstSellers.append(sorted[i]);

    QJsonObject response;
    response["type"] = "publisher_stats_response";
    response["books"] = bookStats;
    response["bestSellers"] = bestSellers;
    response["worstSellers"] = worstSellers;
    response["totalRevenue"] = totalRevenue;
    response["totalBooksCount"] = myBooks.size();
    sendResponse(socket, response);
}

// ================= پنل مدیر: نظارت بر کتاب‌ها و نظرات =================

void Server::handleAdminGetBooks(QTcpSocket* socket) {
    QJsonArray books = enrichBooksWithRatings(loadBooks());

    QJsonObject response;
    response["type"] = "admin_books_response";
    response["books"] = books;
    sendResponse(socket, response);
}

void Server::handleAdminUpdateBook(QTcpSocket* socket, const QJsonObject& data) {
    QString bookId = data["book_id"].toString();

    QJsonArray books = loadBooks();
    QJsonObject response;
    response["type"] = "admin_update_book_response";

    for (int i = 0; i < books.size(); i++) {
        QJsonObject book = books[i].toObject();
        if (book["id"].toString() != bookId) continue;

        if (data.contains("title")) book["title"] = data["title"].toString();
        if (data.contains("author")) book["author"] = data["author"].toString();
        if (data.contains("genre")) book["genre"] = data["genre"].toString();
        if (data.contains("description")) book["description"] = data["description"].toString();
        if (data.contains("price")) book["price"] = data["price"].toDouble();
        if (data.contains("discountPercent")) book["discountPercent"] = data["discountPercent"].toDouble();
        if (data.contains("coverImage")) book["coverImage"] = data["coverImage"].toString();
        if (data.contains("fileURL")) book["fileURL"] = data["fileURL"].toString();
        book["isFree"] = book.value("price").toDouble() <= 0;

        books[i] = book;
        saveBooks(books);
        response["success"] = true;
        response["message"] = "اطلاعات کتاب به‌روزرسانی شد";
        sendResponse(socket, response);
        return;
    }

    response["success"] = false;
    response["message"] = "کتاب یافت نشد";
    sendResponse(socket, response);
}

void Server::handleAdminDeleteBook(QTcpSocket* socket, const QJsonObject& data) {
    QString bookId = data["book_id"].toString();

    QJsonArray books = loadBooks();
    QJsonArray updated;
    bool found = false;

    for (const QJsonValue &v : books) {
        QJsonObject book = v.toObject();
        if (book["id"].toString() == bookId) found = true;
        else updated.append(book);
    }

    QJsonObject response;
    response["type"] = "admin_delete_book_response";
    if (found) {
        saveBooks(updated);
        response["success"] = true;
        response["message"] = "کتاب حذف شد";
    } else {
        response["success"] = false;
        response["message"] = "کتاب یافت نشد";
    }
    sendResponse(socket, response);
}

void Server::handleAdminGetReviews(QTcpSocket* socket) {
    QJsonArray reviews = loadReviews();
    QJsonArray books = loadBooks();

    QJsonArray enriched;
    for (const QJsonValue &v : reviews) {
        QJsonObject review = v.toObject();
        QString bookId = review.value("book_id").toString();

        QString bookTitle = "نامشخص";
        for (const QJsonValue &bv : books) {
            QJsonObject book = bv.toObject();
            if (book["id"].toString() == bookId) {
                bookTitle = book["title"].toString();
                break;
            }
        }
        review["bookTitle"] = bookTitle;
        enriched.append(review);
    }

    QJsonObject response;
    response["type"] = "admin_reviews_response";
    response["reviews"] = enriched;
    sendResponse(socket, response);
}

void Server::handleAdminDeleteReview(QTcpSocket* socket, const QJsonObject& data) {
    QString reviewId = data["review_id"].toString();

    QJsonArray reviews = loadReviews();
    QJsonArray updated;
    QString bookId;
    bool found = false;

    for (const QJsonValue &v : reviews) {
        QJsonObject r = v.toObject();
        if (r["review_id"].toString() == reviewId) {
            bookId = r["book_id"].toString();
            found = true;
            continue;
        }
        updated.append(r);
    }

    QJsonObject response;
    response["type"] = "admin_delete_review_response";
    if (found) {
        saveReviews(updated);
        response["success"] = true;
        response["message"] = "نظر حذف شد";
    } else {
        response["success"] = false;
        response["message"] = "نظر یافت نشد";
    }
    sendResponse(socket, response);

    if (found) broadcastReviewsUpdate(bookId);
}

void Server::sendNotification(QTcpSocket* socket, const QString& title, const QString& message) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject notif;
    notif["type"] = "notification";
    notif["title"] = title;
    notif["message"] = message;

    QJsonDocument doc(notif);
    QByteArray data = doc.toJson(QJsonDocument::Compact) + "\n";

    socket->write(data);
    socket->flush();
}

void Server::broadcastNotification(const QString& title, const QString& message) {
    for (QTcpSocket* socket : m_buffers.keys()) {
        sendNotification(socket, title, message);
    }
}

void Server::sendGenreNotification(const QString& bookGenre, const QString& bookTitle, const QString& bookAuthor) {
    QJsonArray users = loadUsers();

    QSet<QString> interestedUsernames;
    for (const QJsonValue& userValue : users) {
        QJsonObject userObj = userValue.toObject();
        QString role = userObj["role"].toString();

        if (role == "Customer") {
            QJsonArray favoriteGenres = userObj["favoriteGenres"].toArray();
            for (const QJsonValue& g : favoriteGenres) {
                if (g.toString().trimmed().compare(bookGenre.trimmed(), Qt::CaseInsensitive) == 0) {
                    interestedUsernames.insert(userObj["username"].toString());
                    break;
                }
            }
        }
    }

    if (interestedUsernames.isEmpty()) {
        return;
    }

    for (QTcpSocket* clientSocket : m_clients) {
        if (!clientSocket || clientSocket->state() != QAbstractSocket::ConnectedState)
            continue;

        QString clientUsername = clientSocket->property("username").toString();

        if (interestedUsernames.contains(clientUsername)) {
            QString title = "کتاب جدید در ژانر مورد علاقه شما!";
            QString message = QString("کتاب جدیدی با عنوان «%1» اثر %2 در ژانر «%3» منتشر شد!")
                                  .arg(bookTitle, bookAuthor, bookGenre);

            sendNotification(clientSocket, title, message);
        }
    }
}

void Server::sendDiscountNotification(const QString& bookId, const QString& bookTitle, double discountPercent, double newPrice) {
    QJsonArray users = loadUsers();

    for (const QJsonValue& userVal : users) {
        QJsonObject userObj = userVal.toObject();
        if (userObj["role"].toString() != "Customer")
            continue;

        QString targetUsername = userObj["username"].toString();
        bool isInterested = false;

        QJsonArray favorites = userObj["favorites"].toArray();
        for (const QJsonValue& f : favorites) {
            if (f.isString() && f.toString() == bookId) { isInterested = true; break; }
            if (f.isObject() && f.toObject()["id"].toString() == bookId) { isInterested = true; break; }
        }

        if (!isInterested) {
            QJsonArray savedBooks = userObj["savedBooks"].toArray();
            for (const QJsonValue& s : savedBooks) {
                if (s.isString() && s.toString() == bookId) { isInterested = true; break; }
                if (s.isObject() && s.toObject()["id"].toString() == bookId) { isInterested = true; break; }
            }
        }

        if (isInterested) {
            for (QTcpSocket* clientSocket : m_clients) {
                if (clientSocket && clientSocket->state() == QAbstractSocket::ConnectedState) {
                    if (clientSocket->property("username").toString() == targetUsername) {
                        QString title = "🔥 تخفیف ویژه روی کتاب مورد علاقه شما!";
                        QString message = QString("کتاب «%1» که در لیست علاقه‌مندی‌ها/ذخیره‌شده‌های شما قرار دارد، شامل %2٪ تخفیف شد!\nقیمت جدید: %3 تومان")
                                              .arg(bookTitle)
                                              .arg(discountPercent)
                                              .arg(newPrice);

                        sendNotification(clientSocket, title, message);
                    }
                }
            }
        }
    }
}

void Server::handleToggleFavorite(QTcpSocket* socket, const QJsonObject& data) {
    QString username = data["username"].toString();
    QString bookId = data["book_id"].toString();

    QJsonArray users = loadUsers();
    QJsonObject response;
    response["type"] = "toggle_favorite_response";

    for (int i = 0; i < users.size(); ++i) {
        QJsonObject user = users[i].toObject();
        if (user["username"].toString() == username) {
            QJsonArray favorites = user["favorites"].toArray();
            bool exists = false;

            for (int j = 0; j < favorites.size(); ++j) {
                if (favorites[j].toString() == bookId) {
                    favorites.removeAt(j);
                    exists = true;
                    break;
                }
            }

            if (!exists) {
                favorites.append(bookId);
                response["action"] = "added";
                response["message"] = "کتاب به علاقه‌مندی‌ها اضافه شد";
            } else {
                response["action"] = "removed";
                response["message"] = "کتاب از علاقه‌مندی‌ها حذف شد";
            }

            user["favorites"] = favorites;
            users[i] = user;
            saveUsers(users);

            response["success"] = true;
            response["book_id"] = bookId;
            sendResponse(socket, response);
            return;
        }
    }

    response["success"] = false;
    sendResponse(socket, response);
}

void Server::saveNotificationToFile(const QString &username, const QString &title, const QString &message) {
    QFile file("notifications.json");
    QJsonArray notifArray;

    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isArray()) {
            notifArray = doc.array();
        }
        file.close();
    }

    QJsonObject newNotif;
    newNotif["id"] = QUuid::createUuid().toString(); // شناسه یکتا برای هر اعلان
    newNotif["username"] = username;
    newNotif["title"] = title;
    newNotif["message"] = message;
    newNotif["isRead"] = false;
    newNotif["date"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");

    notifArray.append(newNotif);

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(notifArray).toJson());
        file.close();
    }
}

void Server::handleGetNotifications(QTcpSocket *socket, const QJsonObject &request) {
    QString username = request["username"].toString();
    QJsonArray userNotifs;

    QFile file("notifications.json");
    if (file.open(QIODevice::ReadOnly)) {
        QJsonArray allNotifs = QJsonDocument::fromJson(file.readAll()).array();
        file.close();

        for (const QJsonValue &val : allNotifs) {
            QJsonObject obj = val.toObject();
            if (obj["username"].toString() == username) {
                userNotifs.append(obj);
            }
        }
    }

    QJsonObject response;
    response["type"] = "get_notifications_response";
    response["status"] = "success";
    response["notifications"] = userNotifs;
    sendResponse(socket, response);
}

void Server::handleMarkNotificationAsRead(QTcpSocket *socket, const QJsonObject &request) {
    QString notifId = request["notif_id"].toString();
    QFile file("notifications.json");

    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonArray allNotifs = QJsonDocument::fromJson(file.readAll()).array();
    file.close();

    bool updated = false;
    for (int i = 0; i < allNotifs.size(); ++i) {
        QJsonObject obj = allNotifs[i].toObject();
        if (obj["id"].toString() == notifId) {
            obj["isRead"] = true;
            allNotifs[i] = obj;
            updated = true;
            break;
        }
    }

    if (updated && file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(allNotifs).toJson());
        file.close();
    }

    QJsonObject response;
    response["type"] = "mark_notification_read_response";
    response["status"] = updated ? "success" : "error";
    sendResponse(socket, response);
}
void Server::sendNotificationToPublisher(const QString &publisher, const QString &title, const QString &message)
{
    saveNotificationToFile(publisher, title, message);

    QJsonObject notif;
    notif["type"] = "notification";
    notif["title"] = title;
    notif["message"] = message;

    QByteArray data = QJsonDocument(notif).toJson(QJsonDocument::Compact) + "\n";

    for (QTcpSocket *client : m_clients) {
        if (client && client->property("username").toString() == publisher) {
            client->write(data);
            client->flush();
            break;
        }
    }
}