#pragma once

#include <QAbstractTableModel>
#include <QJsonArray>
#include <QString>
#include <QStringList>

/**
 * @brief Modele de table lazy pour afficher des donnees JSON d'entite.
 *
 * Stocke les lignes sous forme de QJsonArray. Aucun QTableWidgetItem
 * n'est instancie : Qt rend uniquement les cellules visibles a l'ecran
 * via le pattern Model/View. Resultat : pas de freeze UI meme avec
 * 100 000 lignes (le bottleneck reste le transfert reseau).
 *
 * Usage :
 *   auto* model = new EntityDataTableModel(this);
 *   model->setRows(initialJsonArray);  // premier batch
 *   tableView->setModel(model);
 *
 *   // Plus tard, pour l'infinite scroll :
 *   model->appendRows(nextBatchJsonArray);
 *
 * Les colonnes sont determinees automatiquement a partir de l'union
 * des cles JSON rencontrees dans les objets. Si un objet n'a pas une
 * cle, la cellule correspondante affiche une chaine vide.
 */
class EntityDataTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit EntityDataTableModel(QObject* parent = nullptr);

    // ── QAbstractTableModel overrides ───────────────────────────
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    // ── API publique ────────────────────────────────────────────

    /**
     * Reinitialise le modele avec les premieres lignes. Recalcule les
     * en-tetes a partir des cles des objets JSON.
     */
    void setRows(const QJsonArray& rows);

    /**
     * Ajoute des lignes a la fin du modele (infinite scroll). Si de
     * nouvelles cles JSON apparaissent, les colonnes sont etendues.
     */
    void appendRows(const QJsonArray& rows);

    /**
     * Vide completement le modele.
     */
    void clear();

    /**
     * Nombre total de lignes connues (donnee par l'API si le mode de
     * pagination le fournit, ou -1 si inconnu).
     */
    void setKnownTotal(int total) { _knownTotal = total; }
    [[nodiscard]] int knownTotal() const { return _knownTotal; }

private:
    /**
     * Reconstruit la liste des colonnes a partir des cles JSON
     * rencontrees dans _rows.
     */
    void rebuildColumns();

    /**
     * Convertit une QJsonValue en QString pour l'affichage cellule.
     * Gere null, bool, number, string, array, object.
     */
    [[nodiscard]] static QString jsonValueToString(const QJsonValue& value);

    QJsonArray  _rows;
    QStringList _columns;
    int         _knownTotal = -1;  // -1 = inconnu (mode non pagine ou cursor)
};