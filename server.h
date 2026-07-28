#ifndef SERVER_H
#define SERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QByteArray>
#include <QList>
//#include "BookManager.h"
//#include "Book.h"

class Server : public QTcpServer
{
    Q_OBJECT
public:
    explicit Server(QObject *parent = nullptr);
    void startServer();
    void sendNotification(QTcpSocket* socket, const QString& title, const QString& message);
    void broadcastNotification(const QString& title, const QString& message);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void readyRead();
    void disconnected();
    void handleGetUsers(QTcpSocket* socket);


private:
    // بافر ورودی هر کلاینت؛ چون چند پیام JSON پشت‌سرهم می‌توانند در یک بسته‌ی
    // TCP به هم بچسبند، پیام‌ها با کاراکتر '\n' جدا می‌شوند و اینجا تا رسیدن
    // یک خط کامل نگه داشته می‌شوند.
    QHash<QTcpSocket*, QByteArray> m_buffers;
    void sendResponse(QTcpSocket* socket, const QJsonObject& response);
    void processMessage(QTcpSocket* socket, const QJsonObject& json);

    // لیست همه‌ی کلاینت‌های وصل، برای ارسال به‌روزرسانی‌های لحظه‌ای (مثل نظر/امتیاز جدید)
    // به تمام کاربران متصل، نه فقط کسی که درخواست را فرستاده
    QList<QTcpSocket*> m_clients;
    void broadcastReviewsUpdate(const QString &bookId);

    // متد کمکی برای ذخیره نوتیفیکیشن جدید در فایل notifications.json
    void saveNotificationToFile(const QString &username, const QString &title, const QString &message);

    // هندرها برای درخواست‌های کلاینت
    void handleGetNotifications(QTcpSocket *socket, const QJsonObject &request);
    void handleMarkNotificationAsRead(QTcpSocket *socket, const QJsonObject &request);


    void sendNotificationToPublisher(const QString &publisher, const QString &title, const QString &message);




    // --- Auth / User management ---
    void handleLogin(QTcpSocket* socket, const QJsonObject& data);
    void handleRegister(QTcpSocket* socket, const QJsonObject& data);
    QJsonArray loadUsers();
    void saveUsers(const QJsonArray& users);
    void handleBlockUser(QTcpSocket* socket, const QJsonObject& data);
    void handleDeleteUser(QTcpSocket* socket, const QJsonObject& data);
    void handleSaveGenres(QTcpSocket *socket, const QJsonObject &data);
    void handleGetSecurityQuestion(QTcpSocket* socket, const QJsonObject& data);
    void handleVerifySecurityAnswer(QTcpSocket* socket, const QJsonObject& data);
    void handleResetPassword(QTcpSocket* socket, const QJsonObject& data);
    void handleGetProfile(QTcpSocket* socket, const QJsonObject& data);
    void handleChangePassword(QTcpSocket* socket, const QJsonObject& data);
    void handleGetLibrary(QTcpSocket* socket, const QJsonObject& data);
    void handleGetPurchaseHistory(QTcpSocket* socket, const QJsonObject& data);
    void handleSearchBooks(QTcpSocket* socket, const QJsonObject& data);

    // --- Books ---
    QJsonArray loadBooks();
    void handleGetBooks(QTcpSocket* socket);
    void saveBooks(const QJsonArray &books);
    double effectivePrice(const QJsonObject &book);

    // --- Cart ---
    QJsonArray loadCarts();
    void saveCarts(const QJsonArray& carts);
    void handleAddToCart(QTcpSocket* socket, const QJsonObject& data);
    void handleGetCart(QTcpSocket* socket, const QJsonObject& data);
    void handleRemoveFromCart(QTcpSocket* socket, const QJsonObject& data);
    void handleCheckout(QTcpSocket* socket, const QJsonObject& data);

    // --- Purchase history & personal library ---
    QJsonArray loadPurchaseHistory();
    void savePurchaseHistory(const QJsonArray &history);
    void addBooksToLibrary(const QString &username, const QJsonArray &purchasedItems);

    // --- Reviews / Ratings ---
    QJsonArray loadReviews();
    void saveReviews(const QJsonArray &reviews);
    void calculateAverageRating(const QString &bookId, double &avgOut, int &countOut);
    QJsonArray enrichBooksWithRatings(QJsonArray books);
    void handlePostReview(QTcpSocket* socket, const QJsonObject& data);
    void handleEditReview(QTcpSocket* socket, const QJsonObject& data);
    void handleDeleteReview(QTcpSocket* socket, const QJsonObject& data);
    void handleGetReviews(QTcpSocket* socket, const QJsonObject& data);

    // --- Saved books (wishlist) ---
    void handleSaveBook(QTcpSocket* socket, const QJsonObject& data);
    void handleUnsaveBook(QTcpSocket* socket, const QJsonObject& data);
    void handleGetSavedBooks(QTcpSocket* socket, const QJsonObject& data);

    // --- قفسه‌ها و دسته‌بندی‌های شخصی ---
    void handleCreateShelf(QTcpSocket* socket, const QJsonObject& data);
    void handleDeleteShelf(QTcpSocket* socket, const QJsonObject& data);
    void handleAddBookToShelf(QTcpSocket* socket, const QJsonObject& data);
    void handleRemoveBookFromShelf(QTcpSocket* socket, const QJsonObject& data);
    void handleGetShelves(QTcpSocket* socket, const QJsonObject& data);
    void sendShelvesResponse(QTcpSocket* socket, const QString &username, bool success, const QString &message);

    // --- پیشرفت مطالعه (آخرین صفحه خوانده‌شده هر کتاب برای هر کاربر) ---
    void handleGetReadingProgress(QTcpSocket* socket, const QJsonObject& data);
    void handleSaveReadingProgress(QTcpSocket* socket, const QJsonObject& data);

    // --- پنل ناشر ---
    void handlePublishBook(QTcpSocket* socket, const QJsonObject& data);
    void handleUpdateBook(QTcpSocket* socket, const QJsonObject& data);
    void handleSetBookActive(QTcpSocket* socket, const QJsonObject& data, bool active);
    void handleGetPublisherBooks(QTcpSocket* socket, const QJsonObject& data);
    void handleGetPublisherStats(QTcpSocket* socket, const QJsonObject& data);

    // --- پنل مدیر: نظارت بر کتاب‌ها و نظرات ---
    void handleAdminGetBooks(QTcpSocket* socket);
    void handleAdminUpdateBook(QTcpSocket* socket, const QJsonObject& data);
    void handleAdminDeleteBook(QTcpSocket* socket, const QJsonObject& data);
    void handleAdminGetReviews(QTcpSocket* socket);
    void handleAdminDeleteReview(QTcpSocket* socket, const QJsonObject& data);

    void sendGenreNotification(const QString& bookGenre, const QString& bookTitle, const QString& bookAuthor);
    void sendDiscountNotification(const QString& bookId, const QString& bookTitle, double discountPercent, double newPrice);

    void handleToggleFavorite(QTcpSocket* socket, const QJsonObject& data);
};

#endif // SERVER_H