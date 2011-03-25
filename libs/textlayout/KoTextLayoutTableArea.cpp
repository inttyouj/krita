/* This file is part of the KDE project
 * Copyright (C) 2009 Elvis Stansvik <elvstone@gmail.org>
 * Copyright (C) 2011 Casper Boemann <cbo@kogmbh.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "KoTextLayoutTableArea.h"

#include "TableIterator.h"
#include "KoTextLayoutArea.h"

#include <KoTableColumnAndRowStyleManager.h>
#include <KoTableColumnStyle.h>
#include <KoTableRowStyle.h>
#include <KoTableCellStyle.h>

#include <QtGlobal>
#include <QTextTable>
#include <QTextTableFormat>
#include <QVector>
#include <QRectF>
#include <QVector>


class KoTextLayoutTableArea::Private
{
public:
    Private()
    {
    }
    KoTextLayoutArea *parent;
    QVector<QVector<KoTextLayoutArea *> > cellAreas;
    TableIterator *startOfArea;
    TableIterator *endOfArea;
    QTextTable *table;
    int headerRows;
    qreal headerOffsetX;
    qreal headerOffsetY;
    KoTableColumnAndRowStyleManager carsManager;
    qreal tableWidth;
    QVector<qreal> rowPositions; // we will only fill those that this area covers
    QVector<qreal> columnWidths;
    QVector<qreal> columnPositions;
};


KoTextLayoutTableArea::KoTextLayoutTableArea(QTextTable *table, KoTextLayoutArea *parent, KoTextDocumentLayout *documentLayout)
  : KoTextLayoutArea(0, documentLayout)
  , d(new Private)
{
    Q_ASSERT(table);
    Q_ASSERT(parent);

    d->table = table;
    d->parent = parent;

    // Resize geometry vectors for the table.
    d->rowPositions.resize(table->rows() + 1);
    d->cellAreas.resize(table->rows());
    for (int row = 0; row < table->rows(); ++row) {
        d->cellAreas[row].resize(table->columns());
    }
}

KoTextLayoutTableArea::~KoTextLayoutTableArea()
{
    for (int row = d->startOfArea->row; row < d->table->rows(); ++row) {
        for (int col = 0; col < d->table->columns(); ++col) {
            delete d->cellAreas[row][col];
        }
    }
    delete d->startOfArea;
    delete d->endOfArea;
}

bool KoTextLayoutTableArea::layout(TableIterator *cursor)
{
    d->startOfArea = new TableIterator(cursor);
    d->headerRows = cursor->headerRows;

    // If table is done we create an empty area and return true
    if (cursor->row == d->table->rows()) {
        setBottom(top());
        d->endOfArea = new TableIterator(cursor);
        return true;
    }
    layoutColumns();

    bool first = cursor->row == 0 && (d->cellAreas[0][0] == 0);
    if (first) { // are we at the beginning of the table
        cursor->row = 0;
        d->rowPositions[0] = top() + d->table->format().topMargin();
        d->headerOffsetX = 0;
        d->headerOffsetY = 0;
    } else {
        for (int row = 0; row < d->headerRows; ++row) {
            // Copy header rows
            d->rowPositions[row] = cursor->headerRowPositions[row];
            for (int col = 0; col < d->table->columns(); ++col) {
                d->cellAreas[row][col] = cursor->headerCellAreas[row][col];
            }
        }

        if (d->headerRows) {
            // Also set the position of the border below headers
            d->rowPositions[d->headerRows] = cursor->headerRowPositions[d->headerRows];
        }

        // If headerRows == 0 then the following reduces to: d->rowPositions[cursor->row] = top()
        d->headerOffsetY = top() - d->rowPositions[0];
        d->rowPositions[cursor->row] = d->rowPositions[d->headerRows] + d->headerOffsetY;

        // headerOffsetX should also be set
        d->headerOffsetX = d->columnPositions[0] - cursor->headerPositionX;
    }

    bool complete = first;
    do {
        complete = layoutRow(cursor);
        setBottom(d->rowPositions[cursor->row]); // This works even when "pagebreaking"
        if (complete) {
            cursor->row++;
            for (int col = 0; col < d->table->columns(); ++col) {
                delete cursor->frameIterators[col];
                cursor->frameIterators[col] = 0;
            }
        }
    } while (complete && cursor->row < d->table->rows());

    if (first) { // were we at the beginning of the table
        for (int row = 0; row < d->headerRows; ++row) {
            // Copy header rows
            cursor->headerRowPositions[row] = d->rowPositions[row];
            for (int col = 0; col < d->table->columns(); ++col) {
                cursor->headerCellAreas[row][col] = d->cellAreas[row][col];
            }
        }
        if (d->headerRows) {
            // Also set the position of the border below headers
            cursor->headerRowPositions[d->headerRows] = d->rowPositions[d->headerRows];
        }
        cursor->headerPositionX = d->columnPositions[0];
    }

    d->endOfArea = new TableIterator(cursor);
    return complete;
}


void KoTextLayoutTableArea::layoutColumns()
{
    QTextTableFormat tableFormat = d->table->format();

    // Table width.
    d->tableWidth = 0;
    qreal parentWidth = right() - left();

    if (tableFormat.width().rawValue() == 0 || tableFormat.alignment() == Qt::AlignJustify) {
        // We got a zero width value or alignment is justify, so use 100% of parent.
        d->tableWidth = parentWidth - tableFormat.leftMargin() - tableFormat.rightMargin();
    } else {
        if (tableFormat.width().type() == QTextLength::FixedLength) {
            // Fixed length value, so use the raw value directly.
            d->tableWidth = tableFormat.width().rawValue();
        } else if (tableFormat.width().type() == QTextLength::PercentageLength) {
            // Percentage length value, so use a percentage of parent width.
            d->tableWidth = tableFormat.width().rawValue() * (parentWidth / 100)
                - tableFormat.leftMargin() - tableFormat.rightMargin();
        } else {
            // Unknown length type, so use 100% of parent.
            d->tableWidth = parentWidth - tableFormat.leftMargin() - tableFormat.rightMargin();
        }
    }

    d->columnPositions.resize(d->table->columns() + 1);
    d->columnWidths.resize(d->table->columns() + 1);

    // Column widths.
    qreal availableWidth = d->tableWidth; // Width available for columns.
    QList<int> relativeWidthColumns; // List of relative width columns.
    qreal relativeWidthSum = 0; // Sum of relative column width values.
    int numNonStyleColumns = 0;
    for (int col = 0; col < d->columnPositions.size(); ++col) {
        KoTableColumnStyle columnStyle = d->carsManager.columnStyle(col);
        if (columnStyle.hasProperty(KoTableColumnStyle::RelativeColumnWidth)) {
            // Relative width specified. Will be handled in the next loop.
            relativeWidthColumns.append(col);
            relativeWidthSum += columnStyle.relativeColumnWidth();
        } else if (columnStyle.hasProperty(KoTableColumnStyle::ColumnWidth)) {
            // Only width specified, so use it.
            d->columnWidths[col] = columnStyle.columnWidth();
            availableWidth -= columnStyle.columnWidth();
        } else {
            // Neither width nor relative width specified.
            d->columnWidths[col] = 0.0;
            relativeWidthColumns.append(col); // handle it as a relative width column without asking for anything
            ++numNonStyleColumns;
        }
    }

    // Calculate width to those columns that don't actually request it
    qreal widthForNonWidthColumn = ((1.0 - qMin<qreal>(relativeWidthSum, 1.0)) * availableWidth);
    availableWidth -= widthForNonWidthColumn; // might as well do this calc before dividing by numNonStyleColumns
    if (numNonStyleColumns)
        widthForNonWidthColumn /= numNonStyleColumns;

    // Relative column widths have now been summed up and can be distributed.
    foreach (int col, relativeWidthColumns) {
        KoTableColumnStyle columnStyle = d->carsManager.columnStyle(col);
        if (columnStyle.hasProperty(KoTableColumnStyle::RelativeColumnWidth) || columnStyle.hasProperty(KoTableColumnStyle::ColumnWidth)) {
            d->columnWidths[col] =
                qMax<qreal>(columnStyle.relativeColumnWidth() * availableWidth / relativeWidthSum, 0.0);
        } else {
            d->columnWidths[col] = widthForNonWidthColumn;
        }
    }

    // Column positions.
    qreal columnPosition = left();
    qreal columnOffset = tableFormat.leftMargin();
    if (tableFormat.alignment() == Qt::AlignRight) {
        // Table is right-aligned, so add all of the remaining space.
        columnOffset += parentWidth - d->tableWidth;
    }
    if (tableFormat.alignment() == Qt::AlignHCenter) {
        // Table is centered, so add half of the remaining space.
        columnOffset += (parentWidth - d->tableWidth) / 2;
    }
    for (int col = 0; col < d->columnPositions.size(); ++col) {
        d->columnPositions[col] = columnPosition + columnOffset;
        // Increment by this column's width.
        columnPosition += d->columnWidths[col];
    }
}

bool KoTextLayoutTableArea::layoutRow(TableIterator *cursor)
{
    int row = cursor->row;

    Q_ASSERT(row >= 0);
    Q_ASSERT(row < d->table->rows());

    bool allCellsTrue = true;
    QTextTableFormat tableFormat = d->table->format();

    /*
     * Implementation Note:
     *
     * An undocumented behavior of QTextTable::cellAt is that requesting a
     * cell that is covered by a spanning cell will return the cell that
     * spans over the requested cell. Example:
     *
     * +------------+------------+
     * |            |            |
     * |            |            |
     * |            +------------+
     * |            |            |
     * |            |            |
     * +------------+------------+
     *
     * table.cellAt(1, 0).row() // Will return 0.
     *
     * In the code below, we rely on this behavior to determine wheather
     * a cell "vertically" ends in the current row, as those are the only
     * cells that should contribute to the row height.
     */

    KoTableRowStyle rowStyle = d->carsManager.rowStyle(row);
    qreal rowHeight = rowStyle.rowHeight();
    bool rowHasExactHeight = rowStyle.hasProperty(KoTableRowStyle::RowHeight);
    qreal rowBottom;

    if (rowHasExactHeight) {
        rowBottom = d->rowPositions[row] + rowHeight;
    } else {
        rowBottom = d->rowPositions[row] + rowStyle.minimumRowHeight();
    }

    if (rowBottom > maximumAllowedBottom())
        return false; // we can't honour minimum or fixed height so don't even try

    int col = 0;
    while (col < d->table->columns()) {
        // Get the cell format.
        QTextTableCell cell = d->table->cellAt(row, col);
        QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();

        if (row == cell.row() + cell.rowSpan() - 1) {
            /*
             * This cell ends vertically in this row, and hence should
             * contribute to the row height.
             */
            KoTableCellStyle cellStyle(cellFormat);
            KoTextLayoutArea *cellArea = new KoTextLayoutArea(this, documentLayout());

            d->cellAreas[cell.row()][cell.column()] = cellArea;

            qreal maxBottom;
            if (rowHasExactHeight) {
                maxBottom = d->rowPositions[row] + rowHeight;
            } else {
                maxBottom = maximumAllowedBottom();
            }
            maxBottom -= cellStyle.bottomPadding() + cellStyle.bottomBorderWidth();
            cellArea->setReferenceRect(
                    d->columnPositions[col] + cellStyle.leftPadding()
                    + cellStyle.leftBorderWidth(),
                    d->columnPositions[col+cell.columnSpan()] - cellStyle.rightPadding()
                    - cellStyle.rightBorderWidth(),
                    d->rowPositions[cell.row()] + cellStyle.topPadding()
                    + cellStyle.topBorderWidth(),
                    maxBottom);

            FrameIterator *cellCursor = cursor->frameIterator(col);
            allCellsTrue &= cellArea->layout(cellCursor);

            if (!rowHasExactHeight) {
                /*
                 * Now we know how much height this cell contributes to the row,
                 * and can determine wheather the row height will grow.
                 */
                rowBottom = qMax(cellArea->bottom() + cellStyle.bottomPadding() + cellStyle.bottomBorderWidth(), rowBottom);
            }
        }
        col += cell.columnSpan(); // Skip across column spans.
    }

    // TODO should also do the following, if this row fitted but next row doesn't fit at all
    if (!allCellsTrue) {
        // We have to go back and layout all merged cells in this row,
        // that don't end in this row.
        col = 0;
        while (col < d->table->columns()) {
            // Get the cell format.
            QTextTableCell cell = d->table->cellAt(row, col);

            if (row != cell.row() + cell.rowSpan() - 1) {
                QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
                KoTableCellStyle cellStyle(cellFormat);
                KoTextLayoutArea *cellArea = new KoTextLayoutArea(this, documentLayout());

                d->cellAreas[cell.row()][cell.column()] = cellArea;

                cellArea->setReferenceRect(
                        d->columnPositions[col] + cellStyle.leftPadding() + cellStyle.leftBorderWidth(),
                        d->columnPositions[col+cell.columnSpan()] - cellStyle.rightPadding() - cellStyle.rightBorderWidth(),
                        d->rowPositions[cell.row()] + cellStyle.topPadding() + cellStyle.topBorderWidth(),
                        rowBottom - cellStyle.bottomPadding() - cellStyle.bottomBorderWidth());

                FrameIterator *cellCursor =  cursor->frameIterator(col);
                allCellsTrue &= cellArea->layout(cellCursor);
            }
            col += cell.columnSpan(); // Skip across column spans.
        }
    }

    // Adjust Y position of NEXT row.
    // This is nice since the outside layout routine relies on the next row having a correct y position
    // the first row y position is set in layout()
    d->rowPositions[row+1] = rowBottom;

    return allCellsTrue;
}

void KoTextLayoutTableArea::paint(QPainter *painter, const KoTextDocumentLayout::PaintContext &context)
{
    int lastRow = d->endOfArea->row;
    if (d->endOfArea->frameIterators[0] == 0) {
        --lastRow;
    }

    if (lastRow <  d->startOfArea->row) {
        return; // empty
    }

    painter->save();

    // Draw table backgroundd->columnPositions[0]
    QRectF tableRect(d->columnPositions[0], d->rowPositions[d->startOfArea->row], d->tableWidth, d->rowPositions[lastRow+1] - d->rowPositions[d->startOfArea->row]);

    painter->fillRect(tableRect, d->table->format().background());

    // Draw row backgrounds
    for (int row = d->startOfArea->row; row <= lastRow; ++row) {
        QRectF rowRect(d->columnPositions[0], d->rowPositions[row], d->tableWidth, d->rowPositions[row+1] - d->rowPositions[row]);
        KoTableRowStyle rowStyle = d->carsManager.rowStyle(row);
        painter->fillRect(rowRect, rowStyle.background());
    }

    // Draw cell backgrounds using their styles.
    for (int row = d->startOfArea->row; row <= lastRow; ++row) {
        for (int column = 0; column < d->table->columns(); ++column) {
            QTextTableCell tableCell = d->table->cellAt(row, column);
            /*
             * The following check relies on the fact that QTextTable::cellAt()
             * will return the cell that has the span when a covered cell is
             * requested.
             */
            if (row == tableCell.row() && column == tableCell.column()) {
                // This is an actual cell we want to draw, and not a covered one.
                QRectF bRect(cellBoundingRect(tableCell));

                // Possibly paint the background of the cell
                QVariant background(tableCell.format().property(KoTableBorderStyle::CellBackgroundBrush));
                if (!background.isNull()) {
                    painter->fillRect(bRect, qvariant_cast<QBrush>(background));
                }

                // Possibly paint the selection of the entire cell
                foreach(const QAbstractTextDocumentLayout::Selection & selection,   context.textContext.selections) {
                    if (selection.cursor.hasComplexSelection()) {
                        int selectionRow;
                        int selectionColumn;
                        int selectionRowSpan;
                        int selectionColumnSpan;
                        selection.cursor.selectedTableCells(&selectionRow, &selectionRowSpan, &selectionColumn, &selectionColumnSpan);
                        if (row >= selectionRow && column>=selectionColumn
                            && row < selectionRow + selectionRowSpan
                            && column < selectionColumn + selectionColumnSpan) {
                            painter->fillRect(bRect, selection.format.background());
                        }
                    } else if (selection.cursor.selectionStart()  < d->table->firstPosition()
                        && selection.cursor.selectionEnd() > d->table->lastPosition()) {
                        painter->fillRect(bRect, selection.format.background());
                    }
                }

                // Paint the content of the cellArea
                // TODO
                d->cellAreas[row][column]->paint(painter, context);

                //
            }
        }
    }

    painter->restore();

#if 0
    drawFrame(table, painter, context, inTable+1); // this actually only draws the text inside
    QVector<QLineF> accuBlankBorders;
    m_tableLayout.drawBorders(painter, &accuBlankBorders);
    painter->setPen(QPen(QColor(0,0,0,96)));
    painter->drawLines(accuBlankBorders);
#endif
}

QRectF KoTextLayoutTableArea::cellBoundingRect(const QTextTableCell &cell) const
{
    int row = cell.row();
    int rowSpan = cell.rowSpan();
    const int column = cell.column();
    const int columnSpan = cell.columnSpan();

    int lastRow = d->endOfArea->row;
    if (d->endOfArea->frameIterators[0] == 0) {
        --lastRow;
    }
    if (lastRow <  d->startOfArea->row) {
        return QRectF(); // empty
    }

    // Limit cell to within the area
    if (row < d->startOfArea->row) {
        rowSpan -= d->startOfArea->row - row;
        row += d->startOfArea->row - row;
    }
    if (row + rowSpan > lastRow) {
        rowSpan = lastRow - row;
    }

    const qreal width = d->columnPositions[column + columnSpan] - d->columnPositions[column];
    const qreal height = d->rowPositions[row + rowSpan] - d->rowPositions[row];

    return QRectF(d->columnPositions[column], d->rowPositions[row], width, height);
}
