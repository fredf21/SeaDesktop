#include "routelistitemdelegate.h"
#include "routelistmodel.h"

#include <QPainter>
#include <QFontMetrics>
#include <QApplication>

namespace {

// ─────────────────────────────────────────────────────────────
// Couleurs des badges
// ─────────────────────────────────────────────────────────────
struct BadgeColors {
    QColor background;
    QColor text;
    QString label;
};

BadgeColors colorsForMode(const QString& mode)
{
    // Couleurs choisies pour bon contraste sur fond clair
    if (mode == "page") {
        return { QColor(33, 150, 243),  Qt::white, "PAGE" };     // Bleu Material 500
    }
    if (mode == "offset") {
        return { QColor(76, 175, 80),   Qt::white, "OFFSET" };   // Vert Material 500
    }
    if (mode == "cursor") {
        return { QColor(255, 193, 7),   Qt::black, "CURSOR" };   // Jaune Material 500
    }
    return { Qt::transparent, Qt::transparent, QString() };
}

// ─────────────────────────────────────────────────────────────
// Couleurs des methodes HTTP
// ─────────────────────────────────────────────────────────────
QColor colorForHttpMethod(const QString& method)
{
    if (method == "GET")    return QColor(46, 125, 50);     // Vert fonce
    if (method == "POST")   return QColor(21, 101, 192);    // Bleu fonce
    if (method == "PUT")    return QColor(230, 81, 0);      // Orange fonce
    if (method == "DELETE") return QColor(198, 40, 40);     // Rouge fonce
    return Qt::darkGray;
}

// ─────────────────────────────────────────────────────────────
// Dimensions
// ─────────────────────────────────────────────────────────────
constexpr int kHorizontalPadding = 8;
constexpr int kVerticalPadding   = 6;
constexpr int kMethodWidth       = 60;
constexpr int kBadgePadding      = 6;
constexpr int kBadgeHeight       = 18;
constexpr int kInterColumnSpace  = 10;

} // namespace

RouteListItemDelegate::RouteListItemDelegate(QObject* parent)
    : QStyledItemDelegate{parent}
{
}

void RouteListItemDelegate::paint(QPainter* painter,
                                  const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    painter->save();

    // Background (selection / hover)
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(option.rect, option.palette.alternateBase());
    }

    // Recupere les donnees du model
    const QString method    = index.data(RouteListModel::HttpMethodRole).toString();
    const QString path      = index.data(RouteListModel::RoutePathRole).toString();
    const QString entityOp  = index.data(RouteListModel::RouteEntityOperationRole).toString();
    const QString mode      = index.data(RouteListModel::PaginationModeRole).toString();

    const QRect rect = option.rect.adjusted(kHorizontalPadding,
                                            kVerticalPadding,
                                            -kHorizontalPadding,
                                            -kVerticalPadding);

    QFont normalFont = option.font;
    QFont boldFont = normalFont;
    boldFont.setBold(true);

    // ─── 1. Methode HTTP (a gauche, en gras colore) ───────────
    painter->setFont(boldFont);
    painter->setPen(colorForHttpMethod(method));
    const QRect methodRect(rect.left(), rect.top(), kMethodWidth, rect.height());
    painter->drawText(methodRect, Qt::AlignLeft | Qt::AlignVCenter, method);

    // ─── 2. Badge pagination (a droite) si applicable ───────────
    int rightLimit = rect.right();
    if (!mode.isEmpty()) {
        const auto colors = colorsForMode(mode);
        painter->setFont(boldFont);
        const QFontMetrics fm(boldFont);
        const int badgeTextWidth = fm.horizontalAdvance(colors.label);
        const int badgeWidth = badgeTextWidth + 2 * kBadgePadding;

        const QRect badgeRect(rect.right() - badgeWidth,
                              rect.top() + (rect.height() - kBadgeHeight) / 2,
                              badgeWidth,
                              kBadgeHeight);

        // Fond arrondi
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.background);
        painter->drawRoundedRect(badgeRect, 4, 4);

        // Texte du badge
        painter->setPen(colors.text);
        painter->drawText(badgeRect, Qt::AlignCenter, colors.label);

        rightLimit = badgeRect.left() - kInterColumnSpace;
    }

    // ─── 3. Texte "entity / operation" (a droite, avant le badge) ──
    painter->setFont(normalFont);
    // Couleur "secondaire" : texte normal mais plus discret
    QColor secondaryColor = option.palette.color(QPalette::Text);
    secondaryColor.setAlpha(160);
    painter->setPen(secondaryColor);

    const QFontMetrics fmNormal(normalFont);
    const int entityOpWidth = fmNormal.horizontalAdvance(entityOp);
    const int entityOpLeft = std::max(rect.left() + kMethodWidth + kInterColumnSpace,
                                      rightLimit - entityOpWidth);
    const QRect entityOpRect(entityOpLeft,
                             rect.top(),
                             rightLimit - entityOpLeft,
                             rect.height());
    painter->drawText(entityOpRect, Qt::AlignRight | Qt::AlignVCenter, entityOp);

    // ─── 4. Path (au centre, prend l'espace restant) ───────────
    painter->setFont(normalFont);
    painter->setPen(option.palette.color(QPalette::Text));
    const QRect pathRect(rect.left() + kMethodWidth + kInterColumnSpace,
                         rect.top(),
                         entityOpLeft - rect.left() - kMethodWidth - 2 * kInterColumnSpace,
                         rect.height());
    const QString elidedPath = fmNormal.elidedText(
        path, Qt::ElideMiddle, pathRect.width()
        );
    painter->drawText(pathRect, Qt::AlignLeft | Qt::AlignVCenter, elidedPath);

    painter->restore();
}

QSize RouteListItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    Q_UNUSED(index);
    const QFontMetrics fm(option.font);
    const int h = fm.height() + 2 * kVerticalPadding + 4;
    return QSize(option.rect.width(), h);
}
