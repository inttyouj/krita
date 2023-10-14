/*
 * SPDX-FileCopyrightText: 2008 Cyrille Berger <cberger@cberger.net>
 * SPDX-FileCopyrightText: 2010 Geoffry Song <goffrie@gmail.com>
 * SPDX-FileCopyrightText: 2017 Scott Petrovic <scottpetrovic@gmail.com>
 * SPDX-FileCopyrightText: 2021 Nabil Maghfur Usman <nmaghfurusman@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <kis_assistant_tool.h>

#include <kis_debug.h>
#include <klocalizedstring.h>

#include <QPainter>
#include <QPainterPath>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QStandardPaths>
#include <QFile>
#include <QLineF>
#include <QMessageBox>

#include <KoIcon.h>
#include <KoFileDialog.h>
#include <KoViewConverter.h>
#include <KoPointerEvent.h>

#include <canvas/kis_canvas2.h>
#include <kis_abstract_perspective_grid.h>
#include <kis_canvas_resource_provider.h>
#include <kis_cursor.h>
#include <kis_document_aware_spin_box_unit_manager.h>
#include <kis_dom_utils.h>
#include <kis_global.h>
#include <kis_image.h>
#include <kis_painting_assistants_decoration.h>
#include <kis_undo_adapter.h>

#include <KisViewManager.h>

#include "EditAssistantsCommand.h"
#include "PerspectiveAssistant.h"
#include "RulerAssistant.h"
#include "TwoPointAssistant.h"
#include "VanishingPointAssistant.h"

#include <math.h>

KisAssistantTool::KisAssistantTool(KoCanvasBase * canvas)
    : KisTool(canvas, KisCursor::arrowCursor())
    , m_canvas(dynamic_cast<KisCanvas2*>(canvas))
    , m_assistantDrag(0)
    , m_newAssistant(0)
    , m_optionsWidget(0)
    , m_unitManager(new KisDocumentAwareSpinBoxUnitManager(this))
{
    Q_ASSERT(m_canvas);
    setObjectName("tool_assistanttool");
}

KisAssistantTool::~KisAssistantTool()
{
}

void KisAssistantTool::activate(const QSet<KoShape*> &shapes)
{

    KisTool::activate(shapes);

    m_canvas->paintingAssistantsDecoration()->activateAssistantsEditor();
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();

    m_internalMode = MODE_CREATION;
    m_canvas->paintingAssistantsDecoration()->setHandleSize(m_handleSize);


    if (m_optionsWidget) {
        m_canvas->paintingAssistantsDecoration()->deselectAssistant();
        updateToolOptionsUI();
    }
    
    m_canvas->updateCanvas();
    
}

void KisAssistantTool::deactivate()
{
    m_canvas->paintingAssistantsDecoration()->deactivateAssistantsEditor();
    m_canvas->updateCanvas();
    KisTool::deactivate();
}

void KisAssistantTool::beginActionImpl(KoPointerEvent *event)
{
    setMode(KisTool::PAINT_MODE);
    m_origAssistantList = KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants());

    bool newAssistantAllowed = true;

    KisPaintingAssistantsDecorationSP canvasDecoration = m_canvas->paintingAssistantsDecoration();

    if (m_newAssistant) {
        m_internalMode = MODE_CREATION;

        if (!snap(event)) {
            *m_newAssistant->handles().back() = canvasDecoration->snapToGuide(event, QPointF(), false);
        }

        if (m_newAssistant->handles().size() == m_newAssistant->numHandles()) {
            addAssistant();
        } else {
            m_newAssistant->addHandle(new KisPaintingAssistantHandle(canvasDecoration->snapToGuide(event, QPointF(), false)), HandleType::NORMAL);
        }
        m_canvas->updateCanvas();
        return;
    }
    m_handleDrag = 0;
    double minDist = m_handleMaxDist;


    QPointF mousePos = m_canvas->viewConverter()->documentToView(canvasDecoration->snapToGuide(event, QPointF(), false));//m_canvas->viewConverter()->documentToView(event->point);

    // syncs the assistant handles to the handles reference we store in this tool
    // they can get out of sync with the way the actions and paintevents occur
    // we probably need to stop storing a reference in m_handles and call the assistants directly
    m_handles = m_canvas->paintingAssistantsDecoration()->handles();


    Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {

        if (assistant->isLocked()) {
            continue; // let's not modify an assistant that is locked
        }


        // find out which handle on all assistants is closest to the mouse position
        // vanishing points have "side handles", so make sure to include that
        {
            QList<KisPaintingAssistantHandleSP> allAssistantHandles;
            allAssistantHandles.append(assistant->handles());
            allAssistantHandles.append(assistant->sideHandles());

            Q_FOREACH (const KisPaintingAssistantHandleSP handle, allAssistantHandles) {

                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*handle));
                if (dist < minDist) {
                    minDist = dist;
                    m_handleDrag = handle;

                    assistantSelected(assistant); // whatever handle is the closest contains the selected assistant
                }
            }
        }




        if(m_handleDrag && assistant->id() == "perspective") {
            // Look for the handle which was pressed


            if (m_handleDrag == assistant->topLeft()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_dragStart = QPointF(assistant->topRight().data()->x(),assistant->topRight().data()->y());
                m_internalMode = MODE_DRAGGING_NODE;
            } else if (m_handleDrag == assistant->topRight()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_internalMode = MODE_DRAGGING_NODE;
                m_dragStart = QPointF(assistant->topLeft().data()->x(),assistant->topLeft().data()->y());
            } else if (m_handleDrag == assistant->bottomLeft()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_internalMode = MODE_DRAGGING_NODE;
                m_dragStart = QPointF(assistant->bottomRight().data()->x(),assistant->bottomRight().data()->y());
            } else if (m_handleDrag == assistant->bottomRight()) {
                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*m_handleDrag));
                if (dist < minDist) {
                    minDist = dist;
                }
                m_internalMode = MODE_DRAGGING_NODE;
                m_dragStart = QPointF(assistant->bottomLeft().data()->x(),assistant->bottomLeft().data()->y());
            } else if (m_handleDrag == assistant->leftMiddle()) {
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_dragStart = QPointF((assistant->bottomLeft().data()->x()+assistant->topLeft().data()->x())*0.5,
                                      (assistant->bottomLeft().data()->y()+assistant->topLeft().data()->y())*0.5);
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->topLeft().data()->x(),assistant->topLeft().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->bottomLeft().data()->x(),assistant->bottomLeft().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(assistant->topLeft(), HandleType::NORMAL );
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->bottomLeft(), HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            } else if (m_handleDrag == assistant->rightMiddle()) {
                m_dragStart = QPointF((assistant->topRight().data()->x()+assistant->bottomRight().data()->x())*0.5,
                                      (assistant->topRight().data()->y()+assistant->bottomRight().data()->y())*0.5);
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->topRight().data()->x(),assistant->topRight().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->bottomRight().data()->x(),assistant->bottomRight().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(assistant->topRight(), HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->bottomRight(), HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            } else if (m_handleDrag == assistant->topMiddle()) {
                m_dragStart = QPointF((assistant->topLeft().data()->x()+assistant->topRight().data()->x())*0.5,
                                      (assistant->topLeft().data()->y()+assistant->topRight().data()->y())*0.5);
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->topLeft().data()->x(),assistant->topLeft().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->topRight().data()->x(),assistant->topRight().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->topRight(), HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->topLeft(), HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            } else if (m_handleDrag == assistant->bottomMiddle()) {
                m_dragStart = QPointF((assistant->bottomLeft().data()->x()+assistant->bottomRight().data()->x())*0.5,
                                      (assistant->bottomLeft().data()->y()+assistant->bottomRight().data()->y())*0.5);
                m_internalMode = MODE_DRAGGING_TRANSLATING_TWONODES;
                m_selectedNode1 = new KisPaintingAssistantHandle(assistant->bottomLeft().data()->x(),assistant->bottomLeft().data()->y());
                m_selectedNode2 = new KisPaintingAssistantHandle(assistant->bottomRight().data()->x(),assistant->bottomRight().data()->y());
                m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get("perspective")->createPaintingAssistant());
                m_newAssistant->addHandle(assistant->bottomLeft(), HandleType::NORMAL);
                m_newAssistant->addHandle(assistant->bottomRight(), HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode2, HandleType::NORMAL);
                m_newAssistant->addHandle(m_selectedNode1, HandleType::NORMAL);
                m_dragEnd = event->point;
                m_handleDrag = 0;
                m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                return;
            }
            m_snapIsRadial = false;
        }
        else if (m_handleDrag && assistant->handles().size()>1 && (assistant->id() == "ruler" ||
                                                                   assistant->id() == "parallel ruler" ||
                                                                   assistant->id() == "infinite ruler" ||
                                                                   assistant->id() == "spline" ||
                                                                   assistant->id() == "curvilinear-perspective")){
            if (m_handleDrag == assistant->handles()[0]) {
                m_dragStart = *assistant->handles()[1];
            } else if (m_handleDrag == assistant->handles()[1]) {
                m_dragStart = *assistant->handles()[0];
            } else if(assistant->handles().size()==4){
                if (m_handleDrag == assistant->handles()[2]) {
                    m_dragStart = *assistant->handles()[0];
                } else if (m_handleDrag == assistant->handles()[3]) {
                    m_dragStart = *assistant->handles()[1];
                }
            }
            m_snapIsRadial = false;
        } else if (m_handleDrag && assistant->handles().size()>2 && (assistant->id() == "ellipse" ||
                                                                     assistant->id() == "concentric ellipse" ||
                                                                     assistant->id() == "fisheye-point")){
            m_snapIsRadial = false;
            if (m_handleDrag == assistant->handles()[0]) {
                m_dragStart = *assistant->handles()[1];
                m_snapIsRadial = false;
            } else if (m_handleDrag == assistant->handles()[1]) {
                m_dragStart = *assistant->handles()[0];
                m_snapIsRadial = false;
            } else if (m_handleDrag == assistant->handles()[2]) {
                m_dragStart = assistant->getEditorPosition();
                m_radius = QLineF(m_dragStart, *assistant->handles()[0]);
                m_snapIsRadial = true;
            }
        } else if (m_handleDrag && assistant->handles().size()>2 && assistant->id() == "two point") {

            // If the user left the assistant's handles in an invalid
            // state (ie 3rd handle isn't between the 1st and 2nd
            // handle), then compute a sensible value for m_dragStart
            // that respects it
            QList<KisPaintingAssistantHandleSP> handles = assistant->handles();

            const QPointF p1 = *assistant->handles()[0];
            const QPointF p2 = *assistant->handles()[1];
            const QPointF p3 = *assistant->handles()[2];

            qreal size = 0;
            QTransform t = qSharedPointerCast<TwoPointAssistant>(m_newAssistant)->localTransform(p1,p2,p3,&size);
            QTransform inv = t.inverted();
            if (t.map(p1).x() * t.map(p2).x() > 0) {

                // We only care about m_dragStart if user is dragging a VP
                if (m_handleDrag == assistant->handles()[0]) {
                    const QPointF safe_start = QPointF(-1.0*t.map(p1).x(),t.map(p1).y());
                    m_dragStart = inv.map(safe_start);
                } else if (m_handleDrag == assistant->handles()[1]) {
                    const QPointF safe_start = QPointF(-1.0*t.map(p2).x(),t.map(p1).y());
                    m_dragStart = inv.map(safe_start);
                }

                m_snapIsRadial = false;
            } else {
                m_dragStart = *m_handleDrag;
                m_snapIsRadial = false;
            }

        } else if (m_handleDrag && assistant->id() == "vanishing point" &&
                   m_handleDrag == assistant->handles()[0]){
            m_dragStart = assistant->getEditorPosition();
            m_snapIsRadial = false;
        }
    }

    m_currentAdjustment = QPointF();

    if (m_handleDrag) {
        // TODO: Shift-press should now be handled using the alternate actions
        // if (event->modifiers() & Qt::ShiftModifier) {
        //     m_handleDrag->uncache();
        //     m_handleDrag = m_handleDrag->split()[0];
        //     m_handles = m_canvas->view()->paintingAssistantsDecoration()->handles();
        // }
        m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
        return;
    }

    m_assistantDrag.clear();
        Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {
        AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
        
        
        const KisCoordinatesConverter *converter = m_canvas->coordinatesConverter();

        // This code contains the click event behavior.
        QTransform initialTransform = converter->documentToWidgetTransform();
        QPointF actionsPosition = initialTransform.map(assistant->viewportConstrainedEditorPosition(converter, globalEditorWidgetData.boundingSize));

        // for UI editor widget controls with move, show, and delete -- disregard document transforms like rotating and mirroring.
        // otherwise the UI controls get awkward to use when they are at 45 degree angles or the order of controls gets flipped backwards
        QPointF uiMousePosition = initialTransform.map(canvasDecoration->snapToGuide(event, QPointF(), false));
        
        //loop through all activated buttons and see if any are being clicked
        if(globalEditorWidgetData.moveButtonActivated) {
            QPointF iconMovePosition(actionsPosition + globalEditorWidgetData.moveIconPosition);
            QRectF moveRect(iconMovePosition, QSizeF(globalEditorWidgetData.buttonSize, globalEditorWidgetData.buttonSize));

            if (moveRect.contains(uiMousePosition) && !assistant->isLocked()) {
                m_assistantDrag = assistant;
                m_cursorStart = event->point;
                m_internalMode = MODE_EDITING;

                assistantSelected(assistant); // whatever handle is the closest contains the selected assistant

                return;
            }
        }
        if(globalEditorWidgetData.snapButtonActivated) {
            QPointF iconSnapPosition(actionsPosition + globalEditorWidgetData.snapIconPosition);
            QRectF visibleRect(iconSnapPosition, QSizeF(globalEditorWidgetData.buttonSize, globalEditorWidgetData.buttonSize));
            if (visibleRect.contains(uiMousePosition)) {
                newAssistantAllowed = false;
                assistant->setSnappingActive(!assistant->isSnappingActive()); // toggle
                assistant->uncache();//this updates the cache of the assistant, very important.

                assistantSelected(assistant); // whatever handle is the closest contains the selected assistant
            }
        }
        if(globalEditorWidgetData.lockButtonActivated) {
            QPointF iconLockPosition(actionsPosition + globalEditorWidgetData.lockedIconPosition);
            QRectF lockRect(iconLockPosition, QSizeF(globalEditorWidgetData.buttonSize, globalEditorWidgetData.buttonSize));
            if (lockRect.contains(uiMousePosition)) {

                assistant->setLocked(!assistant->isLocked());
                assistantSelected(assistant); // whatever handle is the closest contains the selected assistant
                m_internalMode = MODE_EDITING;

                return;
            }
        }
        if(globalEditorWidgetData.duplicateButtonActivated) {
            QPointF iconDuplicatePosition(actionsPosition + globalEditorWidgetData.duplicateIconPosition);
            QRectF duplicateRect(iconDuplicatePosition,QSizeF(globalEditorWidgetData.buttonSize,globalEditorWidgetData.buttonSize));
            
            if (duplicateRect.contains(uiMousePosition)) {
                //create new assistant from old one
                QMap<KisPaintingAssistantHandleSP, KisPaintingAssistantHandleSP> handleMap;
                m_newAssistant = assistant->clone(handleMap);
                m_newAssistant->copySharedData(assistant);
                m_newAssistant->setDuplicating(true);
                //add assistant to global list and register UNDO/REDO object
                m_canvas->paintingAssistantsDecoration()->addAssistant(m_newAssistant);
                QList<KisPaintingAssistantSP> assistants = m_canvas->paintingAssistantsDecoration()->assistants();
                KUndo2Command *addAssistantCmd = new EditAssistantsCommand(m_canvas, m_origAssistantList, KisPaintingAssistant::cloneAssistantList(assistants), EditAssistantsCommand::ADD, assistants.indexOf(m_newAssistant));
                m_canvas->viewManager()->undoAdapter()->addCommand(addAssistantCmd);
                m_origAssistantList = KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants());
                m_handles = m_canvas->paintingAssistantsDecoration()->handles();
                m_canvas->paintingAssistantsDecoration()->setSelectedAssistant(m_newAssistant);
                assistantDuplicatingFlag = true;
                
                // if assistant is locked simply move the editor widget, not the entire assistant
                if(assistant->isLocked()) {
                    newAssistantAllowed = false;
                    m_internalMode = MODE_DRAGGING_EDITOR_WIDGET;
                    m_dragStart = event->point;
                    m_dragEnd = event->point;
                    m_newAssistant.clear();
                } else {
                    m_assistantDrag = m_newAssistant;
                    m_newAssistant.clear();
                    m_cursorStart = event->point;
                    m_internalMode = MODE_EDITING;
                }
                
                updateToolOptionsUI(); 
                m_canvas->updateCanvas();
                return;
            }

        }
        if(globalEditorWidgetData.deleteButtonActivated) {
            QPointF iconDeletePosition(actionsPosition + globalEditorWidgetData.deleteIconPosition);
            QRectF deleteRect(iconDeletePosition, QSizeF(globalEditorWidgetData.buttonSize, globalEditorWidgetData.buttonSize));
            if (deleteRect.contains(uiMousePosition) && !assistant->isLocked()) {
                removeAssistant(assistant);
                if(m_canvas->paintingAssistantsDecoration()->assistants().isEmpty()) {
                    m_internalMode = MODE_CREATION;
                } else m_internalMode = MODE_EDITING;
                m_canvas->updateCanvas();
                return;
            }

        }
        //if user clicking editor widget background.
        if((QRectF(actionsPosition + QPointF(10, 10), globalEditorWidgetData.boundingSize).adjusted(-2, -2, 2, 2).contains(uiMousePosition))) {
            newAssistantAllowed = false;
            m_internalMode = MODE_DRAGGING_EDITOR_WIDGET;
            assistantSelected(assistant);
            m_dragStart = event->point;
            m_dragEnd = event->point;
            
        } 
        
    }

    if (newAssistantAllowed==true) {//don't make a new assistant when I'm just toggling visibility//
        QString key = m_options.availableAssistantsComboBox->model()->index( m_options.availableAssistantsComboBox->currentIndex(), 0 ).data(Qt::UserRole).toString();
        KConfigGroup cfg = KSharedConfig::openConfig()->group(toolId());
        cfg.writeEntry("AssistantType", key);
        m_newAssistant = toQShared(KisPaintingAssistantFactoryRegistry::instance()->get(key)->createPaintingAssistant());
        if (m_newAssistant->canBeLocal()) {
            m_newAssistant->setLocal(m_options.localAssistantCheckbox->isChecked());
        }
        m_internalMode = MODE_CREATION;
        m_newAssistant->addHandle(new KisPaintingAssistantHandle(canvasDecoration->snapToGuide(event, QPointF(), false)), HandleType::NORMAL);
        if (m_newAssistant->numHandles() <= 1) {
            addAssistant();
        } else {
            m_newAssistant->addHandle(new KisPaintingAssistantHandle(canvasDecoration->snapToGuide(event, QPointF(), false)), HandleType::NORMAL);
        }
    }

    if (m_newAssistant) {
        m_newAssistant->setAssistantGlobalColorCache(m_canvas->paintingAssistantsDecoration()->globalAssistantsColor());
    }

    m_canvas->updateCanvas();
}

void KisAssistantTool::continueActionImpl(KoPointerEvent *event)
{
    KisPaintingAssistantsDecorationSP canvasDecoration = m_canvas->paintingAssistantsDecoration();

    if (m_handleDrag) {
        m_previousHandlePos = *m_handleDrag;
        *m_handleDrag = event->point;

        KisPaintingAssistantSP selectedAssistant = m_canvas->paintingAssistantsDecoration()->selectedAssistant();

        if (!snap(event)) {
            *m_handleDrag = canvasDecoration->snapToGuide(event, QPointF(), false);
        }
        m_handleDrag->uncache();

        m_handleCombine = 0;
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            double minDist = 49.0;
            QPointF mousePos = m_canvas->viewConverter()->documentToView(event->point);
            Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
                if (handle == m_handleDrag)
                    continue;


                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*handle));
                if (dist < minDist) {
                    minDist = dist;
                    m_handleCombine = handle;
                }
            }
        }
        m_canvas->updateCanvas();
    } else if (m_assistantDrag) {
        QPointF newAdjustment = canvasDecoration->snapToGuide(event, QPointF(), false) - m_cursorStart;
        if (event->modifiers() & Qt::ShiftModifier ) {
            newAdjustment = snapToClosestNiceAngle(newAdjustment, QPointF(0, 0), M_PI / 4);
        }
        Q_FOREACH (KisPaintingAssistantHandleSP handle, m_assistantDrag->handles()) {
            *handle += (newAdjustment - m_currentAdjustment);
        }
        if (m_assistantDrag->id()== "vanishing point" || m_assistantDrag->id()== "two point"){
            Q_FOREACH (KisPaintingAssistantHandleSP handle, m_assistantDrag->sideHandles()) {
                *handle += (newAdjustment - m_currentAdjustment);
            }
        }
        m_assistantDrag->uncache();
        m_currentAdjustment = newAdjustment;
        m_canvas->updateCanvas();

    } else if (m_internalMode == MODE_DRAGGING_EDITOR_WIDGET) {

        KisPaintingAssistantSP selectedAssistant = m_canvas->paintingAssistantsDecoration()->selectedAssistant();
        QPointF currentOffset = selectedAssistant->editorWidgetOffset();
        selectedAssistant->setEditorWidgetOffset(currentOffset + (event->point - m_dragEnd));
        m_dragEnd = event->point;

    } else {
        event->ignore();
    }

    bool wasHighlightedNode = m_highlightedNode != 0;
    QPointF mousep = m_canvas->viewConverter()->documentToView(event->point);
    QList <KisPaintingAssistantSP> pAssistant= m_canvas->paintingAssistantsDecoration()->assistants();

    Q_FOREACH (KisPaintingAssistantSP assistant, pAssistant) {
        if(assistant->id() == "perspective") {
            if ((m_highlightedNode = assistant->closestCornerHandleFromPoint(mousep))) {
                if (m_highlightedNode == m_selectedNode1 || m_highlightedNode == m_selectedNode2) {
                    m_highlightedNode = 0;
                } else {
                    m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
                    break;
                }
            }
        }

        //this following bit sets the translations for the vanishing-point handles.
        if(m_handleDrag && assistant->id() == "vanishing point" && assistant->sideHandles().size()==4) {
            //for inner handles, the outer handle gets translated.
            if (m_handleDrag == assistant->sideHandles()[0]) {
                QLineF perspectiveline = QLineF(*assistant->handles()[0],
                                                *assistant->sideHandles()[0]);

                qreal length = QLineF(*assistant->sideHandles()[0],
                                      *assistant->sideHandles()[1]).length();

                if (length < 2.0){
                    length = 2.0;
                }

                length += perspectiveline.length();
                perspectiveline.setLength(length);
                *assistant->sideHandles()[1] = perspectiveline.p2();
            }
            else if (m_handleDrag == assistant->sideHandles()[2]){
                QLineF perspectiveline = QLineF(*assistant->handles()[0], *assistant->sideHandles()[2]);
                qreal length = QLineF(*assistant->sideHandles()[2], *assistant->sideHandles()[3]).length();

                if (length<2.0){
                    length=2.0;
                }

                length += perspectiveline.length();
                perspectiveline.setLength(length);
                *assistant->sideHandles()[3] = perspectiveline.p2();
            } // for outer handles, only the vanishing point is translated, but only if there's an intersection.
            else if (m_handleDrag == assistant->sideHandles()[1]|| m_handleDrag == assistant->sideHandles()[3]){
                QPointF vanishingpoint(0,0);
                QLineF perspectiveline = QLineF(*assistant->sideHandles()[0], *assistant->sideHandles()[1]);
                QLineF perspectiveline2 = QLineF(*assistant->sideHandles()[2], *assistant->sideHandles()[3]);

                if (QLineF(perspectiveline2).intersect(QLineF(perspectiveline), &vanishingpoint) != QLineF::NoIntersection){
                    *assistant->handles()[0] = vanishingpoint;
                }
            }// and for the vanishing point itself, only the outer handles get translated.
            else if (m_handleDrag == assistant->handles()[0]){
                QLineF perspectiveline = QLineF(*assistant->handles()[0], *assistant->sideHandles()[0]);
                QLineF perspectiveline2 = QLineF(*assistant->handles()[0], *assistant->sideHandles()[2]);
                qreal length =  QLineF(*assistant->sideHandles()[0], *assistant->sideHandles()[1]).length();
                qreal length2 = QLineF(*assistant->sideHandles()[2], *assistant->sideHandles()[3]).length();

                if (length < 2.0) {
                    length = 2.0;
                }

                if (length2 < 2.0) {
                    length2=2.0;
                }

                length += perspectiveline.length();
                length2 += perspectiveline2.length();
                perspectiveline.setLength(length);
                perspectiveline2.setLength(length2);
                *assistant->sideHandles()[1] = perspectiveline.p2();
                *assistant->sideHandles()[3] = perspectiveline2.p2();
            }

        }
        if (m_handleDrag && assistant->id() == "two point" && assistant->handles().size() >= 3 &&
            assistant->sideHandles().size() == 8) {

          QList<KisPaintingAssistantHandleSP> hndl = assistant->handles();
          QList<KisPaintingAssistantHandleSP> side_hndl = assistant->sideHandles();

          const bool far_handle_is_dragged =
              m_handleDrag == side_hndl[1] || m_handleDrag == side_hndl[3] ||
              m_handleDrag == side_hndl[5] || m_handleDrag == side_hndl[7];

            if (far_handle_is_dragged) {
                QLineF perspective_line_a, perspective_line_b;
                QPointF vp_new_pos(0,0);
                KisPaintingAssistantHandleSP vp_moved;
                if (m_handleDrag == side_hndl[1] || m_handleDrag == side_hndl[5]) {
                    vp_moved = hndl[0];
                    perspective_line_a = QLineF(*side_hndl[0],*side_hndl[1]);
                    perspective_line_b = QLineF(*side_hndl[4],*side_hndl[5]);
                } else {
                    vp_moved = hndl[1];
                    perspective_line_a = QLineF(*side_hndl[3],*side_hndl[2]);
                    perspective_line_b = QLineF(*side_hndl[6],*side_hndl[7]);
                }
                if (perspective_line_a.intersect(perspective_line_b, &vp_new_pos) != QLineF::NoIntersection) {
                    *vp_moved = vp_new_pos;
                }
            } else {
                QLineF perspective_line_a1;
                QLineF perspective_line_b1;
                QLineF perspective_line_a2;
                QLineF perspective_line_b2;

                perspective_line_a1 = QLineF(*hndl[0], *side_hndl[0]);
                perspective_line_a1.setLength(QLineF(*side_hndl[0],*side_hndl[1]).length());
                perspective_line_a1.translate(*side_hndl[0] - perspective_line_a1.p1());
                *side_hndl[1] = perspective_line_a1.p2();

                perspective_line_b1 = QLineF(*hndl[0], *side_hndl[4]);
                perspective_line_b1.setLength(QLineF(*side_hndl[4],*side_hndl[5]).length());
                perspective_line_b1.translate(*side_hndl[4] - perspective_line_b1.p1());
                *side_hndl[5] = perspective_line_b1.p2();

                perspective_line_a2 = QLineF(*hndl[1], *side_hndl[2]);
                perspective_line_a2.setLength(QLineF(*side_hndl[2],*side_hndl[3]).length());
                perspective_line_a2.translate(*side_hndl[2] - perspective_line_a2.p1());
                *side_hndl[3] = perspective_line_a2.p2();

                perspective_line_b2 = QLineF(*hndl[1], *side_hndl[6]);
                perspective_line_b2.setLength(QLineF(*side_hndl[6],*side_hndl[7]).length());
                perspective_line_b2.translate(*side_hndl[6] - perspective_line_b2.p1());
                *side_hndl[7] = perspective_line_b2.p2();
            }
        }
        // The following section handles rulers that have been set to a
        // constant length
        if (m_handleDrag && (assistant->id() == "ruler" || assistant->id() == "infinite ruler")
            && qSharedPointerCast<RulerAssistant>(assistant)->hasFixedLength()
            && assistant->isAssistantComplete()) {
            
            QSharedPointer<RulerAssistant> ruler = qSharedPointerCast<RulerAssistant>(assistant);
          
            if (m_handleDrag == ruler->handles()[0]) {
                // Dragging handle 1 moves the ruler
                *ruler->handles()[1] += *ruler->handles()[0] - m_previousHandlePos;
            }
            else if (m_handleDrag == ruler->handles()[1]) {
                // Dragging handle 2 only allows rotation
                QPointF center = *ruler->handles()[0];
                QPointF handle = *ruler->handles()[1];
                
                QPointF direction = handle - center;
                qreal distance = sqrt(KisPaintingAssistant::norm2(direction));
                QPointF delta = direction / distance * ruler->fixedLength();
                
                *ruler->handles()[1] = center + delta;
            }
        }
    }
    if (wasHighlightedNode && !m_highlightedNode) {
        m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
    }
}

void KisAssistantTool::endActionImpl(KoPointerEvent *event)
{
    setMode(KisTool::HOVER_MODE);
    //release duplication button flag
    if(assistantDuplicatingFlag) {
        KisPaintingAssistantSP selectedAssistant = m_canvas->paintingAssistantsDecoration()->selectedAssistant();
        selectedAssistant->setDuplicating(false);
        assistantDuplicatingFlag = false;
    }

    if (m_handleDrag || m_assistantDrag) {
        if (m_handleDrag) {
            if (!(event->modifiers() & Qt::ShiftModifier) && m_handleCombine) {
                m_handleCombine->mergeWith(m_handleDrag);
                m_handleCombine->uncache();
                m_handles = m_canvas->paintingAssistantsDecoration()->handles();
            }
            m_handleDrag = m_handleCombine = 0;
        } else {
            m_assistantDrag.clear();
        }
        dbgUI << "creating undo command...";
        KUndo2Command *command = new EditAssistantsCommand(m_canvas, m_origAssistantList, KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants()));
        m_canvas->viewManager()->undoAdapter()->addCommand(command);
        dbgUI << "done";
    } else if(m_internalMode == MODE_DRAGGING_TRANSLATING_TWONODES) {
        addAssistant();
        m_internalMode = MODE_CREATION;
    } else if (m_internalMode == MODE_DRAGGING_EDITOR_WIDGET) {
        KisPaintingAssistantSP selectedAssistant = m_canvas->paintingAssistantsDecoration()->selectedAssistant();
        QPointF currentOffset = selectedAssistant->editorWidgetOffset();
        selectedAssistant->setEditorWidgetOffset(currentOffset + (event->point - m_dragEnd));
    }
    else {
        event->ignore();
    }

    m_canvas->updateCanvas(); // TODO update only the relevant part of the canvas
}

void KisAssistantTool::addAssistant()
{
    m_canvas->paintingAssistantsDecoration()->addAssistant(m_newAssistant);

    // generate the side handles for the Two Point assistant
    if (m_newAssistant->id() == "two point"){
        QList<KisPaintingAssistantHandleSP> handles = m_newAssistant->handles();
        QSharedPointer<TwoPointAssistant> assis = qSharedPointerCast<TwoPointAssistant>(m_newAssistant);

        if (*handles[0] == *handles[1] || *handles[1] == *handles[2]) {
            // Place handles in sensible default position if any of
            // them are overlapping (maybe because user
            // double-clicked)
            const QTransform transform = m_canvas->coordinatesConverter()->documentToWidgetTransform();
            const QTransform inverted = transform.inverted();
            const int size = inverted.map(QPointF(m_canvas->canvasWidget()->width(),0)).x();
            *handles[0] = *handles[2] - QPointF(-size/3,0);
            *handles[1] = *handles[2] - QPointF(size/3,0);
        }

        const QPointF p1 = *handles[0];
        const QPointF p2 = *handles[1];
        const QPointF p3 = *handles[2];

        qreal size = 0;
        QTransform t = assis->localTransform(p1,p2,p3,&size);
        QTransform inv = t.inverted();

        if (t.map(p1).x() * t.map(p2).x() > 0) {
            // Put third handle between first and second if user
            // placed it outside of them, then re-define the transform
            const QLineF horizon = QLineF(t.map(p1),t.map(p2));
            const QPointF origin = QPointF(horizon.center().x(),0);
            *handles[2] = inv.map(origin);
            t = assis->localTransform(p1,p2,*handles[2],&size);
            inv = t.inverted();
        }

        const QPointF above = inv.map(QPointF(0,t.map(p1).y()+size));
        const QPointF below = inv.map(QPointF(0,t.map(p1).y()-size));

        Q_FOREACH (QPointF side, QList<QPointF>({above,below})) {
            Q_FOREACH (QPointF vp, QList<QPointF>({p1, p2})) {
                QLineF bar = QLineF(side, vp);
                m_newAssistant->addHandle(new KisPaintingAssistantHandle(bar.pointAt(0.8)), HandleType::SIDE);
                m_newAssistant->addHandle(new KisPaintingAssistantHandle(bar.pointAt(0.4)), HandleType::SIDE);
            }
        }
    }


    QList<KisPaintingAssistantSP> assistants = m_canvas->paintingAssistantsDecoration()->assistants();
    KUndo2Command *addAssistantCmd = new EditAssistantsCommand(m_canvas, m_origAssistantList, KisPaintingAssistant::cloneAssistantList(assistants), EditAssistantsCommand::ADD, assistants.indexOf(m_newAssistant));
    m_canvas->viewManager()->undoAdapter()->addCommand(addAssistantCmd);

    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    m_canvas->paintingAssistantsDecoration()->setSelectedAssistant(m_newAssistant);
    updateToolOptionsUI(); // vanishing point assistant will get an extra option

    m_newAssistant.clear();
}

void KisAssistantTool::updateEditorWidgetData()
{

    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
    if( !globalEditorWidgetData.moveButtonActivated && !globalEditorWidgetData.snapButtonActivated && !globalEditorWidgetData.lockButtonActivated && 
        !globalEditorWidgetData.duplicateButtonActivated && !globalEditorWidgetData.deleteButtonActivated) {
        globalEditorWidgetData.widgetActivated = false;
    } else globalEditorWidgetData.widgetActivated = true;

    int horizontalButtonLimit = globalEditorWidgetData.horizontalButtonLimit;
    int buttonCount = 0;
    int horizontalButtonCount = 0;
    int positionX = 15;
    int positionY = 15;

    //loop through all buttons and calculate positions
    if(globalEditorWidgetData.moveButtonActivated) {
        
        buttonCount++;
        horizontalButtonCount++;
        if(horizontalButtonCount > horizontalButtonLimit)
        {
            horizontalButtonCount = 1;
            positionX = 15;
            positionY += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
        }
        //the size icon is a little smaller than the others visually, so the icon is scaled
        //and the positioin is adjusted by -5 to compinsate
        globalEditorWidgetData.moveIconPosition.setX(positionX-5);
        globalEditorWidgetData.moveIconPosition.setY(positionY-5);
        positionX += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
    }
    if(globalEditorWidgetData.snapButtonActivated) {
        buttonCount++;
        horizontalButtonCount++;
        if(horizontalButtonCount > horizontalButtonLimit)
        {
            horizontalButtonCount = 1;
            positionX = 15;
            positionY += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
        }
        globalEditorWidgetData.snapIconPosition.setX(positionX);
        globalEditorWidgetData.snapIconPosition.setY(positionY);
        positionX += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
    }
    if(globalEditorWidgetData.lockButtonActivated) {
        buttonCount++;
        horizontalButtonCount++;
        if(horizontalButtonCount > horizontalButtonLimit)
        {
            horizontalButtonCount = 1;
            positionX = 15;
            positionY += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
        }
        globalEditorWidgetData.lockedIconPosition.setX(positionX);
        globalEditorWidgetData.lockedIconPosition.setY(positionY);
        positionX += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
    }
    if(globalEditorWidgetData.duplicateButtonActivated) {
        buttonCount++;
        horizontalButtonCount++;
        if(horizontalButtonCount > horizontalButtonLimit)
        {
            horizontalButtonCount = 0;
            positionX = 15;
            positionY += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
        }
        globalEditorWidgetData.duplicateIconPosition.setX(positionX);
        globalEditorWidgetData.duplicateIconPosition.setY(positionY);
        positionX += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
    }
    if(globalEditorWidgetData.deleteButtonActivated) {
        buttonCount++;
        horizontalButtonCount++;
        if(horizontalButtonCount > horizontalButtonLimit)
        {
            horizontalButtonCount = 1;
            positionX = 15;
            positionY += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
        }
        globalEditorWidgetData.deleteIconPosition.setX(positionX);
        globalEditorWidgetData.deleteIconPosition.setY(positionY);
        positionX += globalEditorWidgetData.buttonSize + globalEditorWidgetData.buttonPadding;
    }
    
    int buttonSection = globalEditorWidgetData.buttonSize+globalEditorWidgetData.buttonPadding;
    int boundingWidgetWidth = (buttonCount < horizontalButtonLimit) ? buttonCount*buttonSection:horizontalButtonLimit*buttonSection;
    boundingWidgetWidth += 5;
    globalEditorWidgetData.boundingSize.setWidth(boundingWidgetWidth+globalEditorWidgetData.dragDecorationWidth);

    int buttonToWidthRatio = (buttonCount/horizontalButtonLimit);
    if(buttonCount%horizontalButtonLimit != 0) {
        buttonToWidthRatio++;
    }

    int boundingWidgetHeight = buttonToWidthRatio*buttonSection;
    boundingWidgetHeight += 5;
    globalEditorWidgetData.boundingSize.setHeight(boundingWidgetHeight);
    
    
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::removeAssistant(KisPaintingAssistantSP assistant)
{
    QList<KisPaintingAssistantSP> assistants = m_canvas->paintingAssistantsDecoration()->assistants();

    m_canvas->paintingAssistantsDecoration()->removeAssistant(assistant);

    KUndo2Command *removeAssistantCmd = new EditAssistantsCommand(m_canvas, m_origAssistantList, KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants()), EditAssistantsCommand::REMOVE, assistants.indexOf(assistant));
    m_canvas->viewManager()->undoAdapter()->addCommand(removeAssistantCmd);

    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    m_canvas->paintingAssistantsDecoration()->deselectAssistant();
    updateToolOptionsUI();
}

void KisAssistantTool::assistantSelected(KisPaintingAssistantSP assistant)
{
     m_canvas->paintingAssistantsDecoration()->setSelectedAssistant(assistant);
     updateToolOptionsUI();
}

void KisAssistantTool::updateToolOptionsUI()
{
     KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();

     bool hasActiveAssistant = m_selectedAssistant ? true : false;
    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;

    if(globalEditorWidgetData.moveButtonActivated) {
        m_options.showMove->setChecked(true);
    }
    if(globalEditorWidgetData.snapButtonActivated) {
        m_options.showSnap->setChecked(true);
    }
    if(globalEditorWidgetData.lockButtonActivated) {
        m_options.showLock->setChecked(true);
    }
    if(globalEditorWidgetData.duplicateButtonActivated) {
        m_options.showDuplicate->setChecked(true);
    }
    if(globalEditorWidgetData.deleteButtonActivated) {
        m_options.showDelete->setChecked(true);
    }

    if (m_selectedAssistant) {
         bool isVanishingPointAssistant = m_selectedAssistant->id() == "vanishing point";
         bool isTwoPointAssistant = m_selectedAssistant->id() == "two point";
         bool isRulerAssistant = m_selectedAssistant->id() == "ruler"
                              || m_selectedAssistant->id() == "infinite ruler";
         bool isPerspectiveAssistant = m_selectedAssistant->id() == "perspective";

         m_options.vanishingPointAngleSpinbox->setVisible(isVanishingPointAssistant);
         m_options.twoPointDensitySpinbox->setVisible(isTwoPointAssistant);
         m_options.twoPointUseVerticalCheckbox->setVisible(isTwoPointAssistant);
         m_options.subdivisionsSpinbox->setVisible(isRulerAssistant || isPerspectiveAssistant);
         m_options.minorSubdivisionsSpinbox->setVisible(isRulerAssistant);
         m_options.fixedLengthCheckbox->setVisible(isRulerAssistant);
         //show checkboxes for controlling which editor widget buttons are visible
         m_options.showMove->setVisible(true);
         m_options.showSnap->setVisible(true);
         m_options.showLock->setVisible(true);
         m_options.showDuplicate->setVisible(true);
         m_options.showDelete->setVisible(true);
         
         

         if (isVanishingPointAssistant) {
             QSharedPointer <VanishingPointAssistant> assis = qSharedPointerCast<VanishingPointAssistant>(m_selectedAssistant);
             m_options.vanishingPointAngleSpinbox->setValue(assis->referenceLineDensity());
         }

         if (isTwoPointAssistant) {
             QSharedPointer <TwoPointAssistant> assis = qSharedPointerCast<TwoPointAssistant>(m_selectedAssistant);
             m_options.twoPointDensitySpinbox->setValue(assis->gridDensity());
             m_options.twoPointUseVerticalCheckbox->setChecked(assis->useVertical());
         }

         if (isRulerAssistant) {
             QSharedPointer <RulerAssistant> assis = qSharedPointerCast<RulerAssistant>(m_selectedAssistant);
             m_options.subdivisionsSpinbox->setValue(assis->subdivisions());
             m_options.minorSubdivisionsSpinbox->setValue(assis->minorSubdivisions());
             m_options.fixedLengthCheckbox->setChecked(assis->hasFixedLength());
             
             m_options.fixedLengthSpinbox->setVisible(assis->hasFixedLength());
             m_options.fixedLengthUnit->setVisible(assis->hasFixedLength());
             {
                // Block valueChanged signals during unit switch
                QSignalBlocker b(m_options.fixedLengthSpinbox);
                m_unitManager->setApparentUnitFromSymbol(assis->fixedLengthUnit());
                m_options.fixedLengthUnit->setCurrentIndex(m_unitManager->getApparentUnitId());
                m_options.fixedLengthSpinbox->changeValue(assis->fixedLength());
             }
             m_options.fixedLengthSpinbox->setPrefix("");
         } else {
             m_options.fixedLengthSpinbox->setVisible(false);
             m_options.fixedLengthUnit->setVisible(false);
         }
         
         if (isPerspectiveAssistant) {
             QSharedPointer <PerspectiveAssistant> assis = qSharedPointerCast<PerspectiveAssistant>(m_selectedAssistant);
             m_options.subdivisionsSpinbox->setValue(assis->subdivisions());
         }
         
         // load custom color settings from assistant (this happens when changing assistant
         m_options.useCustomAssistantColor->setChecked(m_selectedAssistant->useCustomColor());
         m_options.customAssistantColorButton->setColor(m_selectedAssistant->assistantCustomColor());


         double opacity = (double)m_selectedAssistant->assistantCustomColor().alpha()/(double)255.00 * (double)100.00 ;
         opacity = ceil(opacity); // helps keep the 0-100% slider from shifting

         m_options.customColorOpacitySlider->blockSignals(true);
         m_options.customColorOpacitySlider->setValue((double)opacity);
         m_options.customColorOpacitySlider->blockSignals(false);

     } else {
         m_options.vanishingPointAngleSpinbox->setVisible(false);
         m_options.twoPointDensitySpinbox->setVisible(false);
         m_options.twoPointUseVerticalCheckbox->setVisible(false);
         m_options.subdivisionsSpinbox->setVisible(false);
         m_options.minorSubdivisionsSpinbox->setVisible(false);
         m_options.fixedLengthCheckbox->setVisible(false);
         m_options.fixedLengthSpinbox->setVisible(false);
         m_options.fixedLengthUnit->setVisible(false);
     }

     // show/hide elements if an assistant is selected or not
      m_options.useCustomAssistantColor->setVisible(hasActiveAssistant);

      // hide custom color options if use custom color is not selected
      bool showCustomColorSettings = m_options.useCustomAssistantColor->isChecked() && hasActiveAssistant;
      m_options.customColorOpacitySlider->setVisible(showCustomColorSettings);
      m_options.customAssistantColorButton->setVisible(showCustomColorSettings);

      // disable global color settings if we are using the custom color
      m_options.assistantsGlobalOpacitySlider->setEnabled(!showCustomColorSettings);
      m_options.assistantsColor->setEnabled(!showCustomColorSettings);
      m_options.globalColorLabel->setEnabled(!showCustomColorSettings);

      QString key = m_options.availableAssistantsComboBox->model()->index( m_options.availableAssistantsComboBox->currentIndex(), 0 ).data(Qt::UserRole).toString();
      m_options.localAssistantCheckbox->setVisible(key == "two point" || key == "vanishing point" || key == "parallel ruler");

}

void KisAssistantTool::slotChangeVanishingPointAngle(double value)
{
    if ( m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }

    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isVanishingPointAssistant = m_selectedAssistant->id() == "vanishing point";

        if (isVanishingPointAssistant) {
            QSharedPointer <VanishingPointAssistant> assis = qSharedPointerCast<VanishingPointAssistant>(m_selectedAssistant);
            assis->setReferenceLineDensity((float)value);
        }
    }

    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotChangeTwoPointDensity(double value)
{
    if ( m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }

    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isTwoPointAssistant = m_selectedAssistant->id() == "two point";

        if (isTwoPointAssistant) {
            QSharedPointer <TwoPointAssistant> assis = qSharedPointerCast<TwoPointAssistant>(m_selectedAssistant);
            assis->setGridDensity((float)value);
        }
    }

    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotChangeTwoPointUseVertical(int value)
{
    if ( m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }

    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isTwoPointAssistant = m_selectedAssistant->id() == "two point";

        if (isTwoPointAssistant) {
            QSharedPointer <TwoPointAssistant> assis = qSharedPointerCast<TwoPointAssistant>(m_selectedAssistant);
            assis->setUseVertical(value == Qt::Checked);
        }
    }

    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotChangeSubdivisions(int value) {
    if (m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }
    
    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isRulerAssistant = m_selectedAssistant->id() == "ruler"
                             || m_selectedAssistant->id() == "infinite ruler";
        bool isPerspectiveAssistant = m_selectedAssistant->id() == "perspective";
        
        if (isRulerAssistant) {
            QSharedPointer <RulerAssistant> assis = qSharedPointerCast<RulerAssistant>(m_selectedAssistant);
            assis->setSubdivisions(value);
            
            m_options.minorSubdivisionsSpinbox->setEnabled(value > 0);
        }
        
        else if (isPerspectiveAssistant) {
            QSharedPointer <PerspectiveAssistant> assis = qSharedPointerCast<PerspectiveAssistant>(m_selectedAssistant);
            assis->setSubdivisions(value);
        }
    }
    
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotChangeMinorSubdivisions(int value) {
    if (m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }
    
    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isRulerAssistant = m_selectedAssistant->id() == "ruler"
                             || m_selectedAssistant->id() == "infinite ruler";
        
        if (isRulerAssistant) {
            QSharedPointer <RulerAssistant> assis = qSharedPointerCast<RulerAssistant>(m_selectedAssistant);
            assis->setMinorSubdivisions(value);
        }
    }
    
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotEnableFixedLength(int enabled) {
    if (m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }
    
    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isRulerAssistant = m_selectedAssistant->id() == "ruler"
                             || m_selectedAssistant->id() == "infinite ruler";
        
        if (isRulerAssistant) {
            QSharedPointer <RulerAssistant> assis = qSharedPointerCast<RulerAssistant>(m_selectedAssistant);
            
            m_options.fixedLengthSpinbox->setVisible(enabled);
            m_options.fixedLengthUnit->setVisible(enabled);
            
            if (enabled && !assis->hasFixedLength() && assis->handles().size() >= 2) {
                // When enabling, set the length to the current length.
                QPointF a = *assis->handles()[0];
                QPointF b = *assis->handles()[1];
                qreal length = sqrt(KisPaintingAssistant::norm2(b - a));
                assis->setFixedLength(length);
                m_options.fixedLengthSpinbox->changeValue(length);
            }
  
            assis->enableFixedLength(enabled);
        }
    }
    
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotChangeFixedLength(double) {
    // This slot should only be called when the user actually changes the
    // value, not when it is changed through unit conversions. Use
    // QSignalBlocker on the spin box when converting units to achieve this.
  
    if (m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }
    
    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isRulerAssistant = m_selectedAssistant->id() == "ruler"
                             || m_selectedAssistant->id() == "infinite ruler";
        
        if (isRulerAssistant) {
            QSharedPointer <RulerAssistant> assis = qSharedPointerCast<RulerAssistant>(m_selectedAssistant);
            // Stores the newly entered data (length & unit) in the assistant
            assis->setFixedLengthUnit(m_unitManager->getApparentUnitSymbol());
            assis->setFixedLength(m_options.fixedLengthSpinbox->value());
            assis->ensureLength();
            m_options.fixedLengthSpinbox->setPrefix("");
        }
    }
    
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotChangeFixedLengthUnit(int index) {
    if (m_canvas->paintingAssistantsDecoration()->assistants().length() == 0) {
        return;
    }
  
    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =
            m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        bool isRulerAssistant = m_selectedAssistant->id() == "ruler" ||
                                m_selectedAssistant->id() == "infinite ruler";
  
        if (isRulerAssistant) {
            QSharedPointer<RulerAssistant> assis = qSharedPointerCast<RulerAssistant>(m_selectedAssistant);
            
            // Saving and restoring the actual length manually, since the
            // unit manager rounds without warning during conversion
            qreal current_length = assis->fixedLength();
            {
                // Block signals from the spinbox while changing the unit to
                // avoid edits via valueChanged() in the meantime
                QSignalBlocker b(m_options.fixedLengthSpinbox);
                m_unitManager->selectApparentUnitFromIndex(index);
                m_options.fixedLengthSpinbox->changeValue(current_length);
            }
            
            QString unit = m_unitManager->getApparentUnitSymbol();
            
            if (unit == assis->fixedLengthUnit()) {
                // If the previous unit is selected, show no prefix: perfect match
                m_options.fixedLengthSpinbox->setPrefix("");
            }
            else {
                if (abs(m_options.fixedLengthSpinbox->value() - current_length) > 1e-3) {
                    // If the units don't match, show the approximate symbol as prefix
                    m_options.fixedLengthSpinbox->setPrefix(u8"\u2248");
                } else {
                    // If the units don't match but converted perfectly
                    // (close enough: +- 0.001 pt), show equals instead
                    m_options.fixedLengthSpinbox->setPrefix("=");
                }
            }
        }
    }
}

void KisAssistantTool::mouseMoveEvent(KoPointerEvent *event)
{
    m_handleHover = 0;
    if (m_newAssistant && m_internalMode == MODE_CREATION) {

        KisPaintingAssistantHandleSP new_handle = m_newAssistant->handles().back();
        if (!snap(event)) {
            KisPaintingAssistantsDecorationSP canvasDecoration = m_canvas->paintingAssistantsDecoration();
            *new_handle = canvasDecoration->snapToGuide(event, QPointF(), false);
        }

    } else if (m_newAssistant && m_internalMode == MODE_DRAGGING_TRANSLATING_TWONODES) {
        QPointF translate = event->point - m_dragEnd;
        m_dragEnd = event->point;
        m_selectedNode1.data()->operator = (QPointF(m_selectedNode1.data()->x(),m_selectedNode1.data()->y()) + translate);
        m_selectedNode2.data()->operator = (QPointF(m_selectedNode2.data()->x(),m_selectedNode2.data()->y()) + translate);
    } else if (mode() == KisTool::HOVER_MODE) {

        // find a handle underneath...
        double minDist = m_handleMaxDist;

        QPointF mousePos = m_canvas->viewConverter()->documentToView(event->point);

        Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {
            QList<KisPaintingAssistantHandleSP> allAssistantHandles;
            allAssistantHandles.append(assistant->handles());
            allAssistantHandles.append(assistant->sideHandles());

            Q_FOREACH (const KisPaintingAssistantHandleSP handle, allAssistantHandles) {

                double dist = KisPaintingAssistant::norm2(mousePos - m_canvas->viewConverter()->documentToView(*handle));
                if (dist < minDist) {
                    minDist = dist;
                    m_handleHover = handle;
                }
            }
        }
    }

    m_canvas->updateCanvasDecorations();
}


void KisAssistantTool::keyPressEvent(QKeyEvent *event)
{
    // When the user is in the middle of creating a new
    // assistant the escape key can be used to cancel this process.
    if (event->key()==Qt::Key_Escape && (m_newAssistant)) {
        // Clear shared pointer to the assistant being created so
        // it gets cleaned-up
        m_newAssistant.clear();
        m_canvas->updateCanvas();
        event->accept();
    } else {
        event->ignore();
    }
}

void KisAssistantTool::paint(QPainter& _gc, const KoViewConverter &_converter)
{
    QRectF canvasSize = QRectF(QPointF(0, 0), QSizeF(m_canvas->image()->size()));

    // show special display while a new assistant is in the process of being created
    if (m_newAssistant) {

        QColor assistantColor = m_newAssistant->effectiveAssistantColor();
        assistantColor.setAlpha(80);

        m_newAssistant->drawAssistant(_gc, canvasSize, m_canvas->coordinatesConverter(), false, m_canvas, true, false);
        Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_newAssistant->handles()) {
            QPainterPath path;
            path.addEllipse(QRectF(_converter.documentToView(*handle) -  QPointF(m_handleSize * 0.5, m_handleSize * 0.5), QSizeF(m_handleSize, m_handleSize)));

            _gc.save();
            _gc.setPen(Qt::NoPen);
            _gc.setBrush(assistantColor);
            _gc.drawPath(path);
            _gc.restore();
        }
    }


    Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {

        QColor assistantColor = assistant->effectiveAssistantColor();
        assistantColor.setAlpha(80);

        Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
            QRectF ellipse(_converter.documentToView(*handle) -  QPointF(m_handleSize * 0.5, m_handleSize * 0.5),
                           QSizeF(m_handleSize, m_handleSize));

            // render handles differently if it is the one being dragged.
            if (handle == m_handleDrag || handle == m_handleCombine || (handle == m_handleHover && !handle->chiefAssistant()->isLocked())) {
                QPen stroke(assistantColor, 4);
                _gc.save();
                _gc.setPen(stroke);
                _gc.setBrush(Qt::NoBrush);
                _gc.drawEllipse(ellipse);
                _gc.restore();
            }

        }
    }
}

void KisAssistantTool::removeAllAssistants()
{
    m_origAssistantList = m_canvas->paintingAssistantsDecoration()->assistants();

    m_canvas->paintingAssistantsDecoration()->removeAll();

    KUndo2Command *removeAssistantCmd = new EditAssistantsCommand(m_canvas, m_origAssistantList, KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants()));
    m_canvas->viewManager()->undoAdapter()->addCommand(removeAssistantCmd);

    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    m_canvas->updateCanvas();

    m_canvas->paintingAssistantsDecoration()->deselectAssistant();
    updateToolOptionsUI();
}

void KisAssistantTool::loadAssistants()
{
    KoFileDialog dialog(m_canvas->viewManager()->mainWindowAsQWidget(), KoFileDialog::OpenFile, "OpenAssistant");
    dialog.setCaption(i18n("Select an Assistant"));
    dialog.setDefaultDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    dialog.setMimeTypeFilters(QStringList() << "application/x-krita-assistant", "application/x-krita-assistant");
    QString filename = dialog.filename();
    if (filename.isEmpty()) return;
    if (!QFileInfo(filename).exists()) return;

    QFile file(filename);
    file.open(QIODevice::ReadOnly);

    QByteArray data = file.readAll();
    QXmlStreamReader xml(data);
    QMap<int, KisPaintingAssistantHandleSP> handleMap;
    QMap<int, KisPaintingAssistantHandleSP> sideHandleMap;
    KisPaintingAssistantSP assistant;
    bool errors = false;

    m_origAssistantList = KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants());


    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement:
            if (xml.name() == "handle") {
                if (assistant && !xml.attributes().value("ref").isEmpty()) {
                    KisPaintingAssistantHandleSP handle = handleMap.value(xml.attributes().value("ref").toString().toInt());
                    if (handle) {
                       assistant->addHandle(handle, HandleType::NORMAL);
                    } else {
                        errors = true;
                    }
                } else {
                    QString strId = xml.attributes().value("id").toString(),
                            strX = xml.attributes().value("x").toString(),
                            strY = xml.attributes().value("y").toString();



                    if (!strId.isEmpty() && !strX.isEmpty() && !strY.isEmpty()) {
                        int id = strId.toInt();
                        double x = strX.toDouble(),
                                y = strY.toDouble();
                        if (!handleMap.contains(id)) {
                            handleMap.insert(id, new KisPaintingAssistantHandle(x, y));
                        } else {
                            errors = true;
                        }
                    } else {
                        errors = true;
                    }
                }
            // for vanishing point assistant
            } else if (xml.name() == "sidehandle"){

              // read in sidehandles
              if (!xml.attributes().value("id").isEmpty()) {
                  QString strId = xml.attributes().value("id").toString(),
                          strX = xml.attributes().value("x").toString(),
                          strY = xml.attributes().value("y").toString();
                  if (!strId.isEmpty() && !strX.isEmpty() && !strY.isEmpty()) {
                      int id = strId.toInt();
                      double x = strX.toDouble();
                      double y = strY.toDouble();
                      if (!sideHandleMap.contains(id)) {
                          sideHandleMap.insert(id, new KisPaintingAssistantHandle(x,y));
                      }}
              }
              // addHandle to assistant
              if (!xml.attributes().value("ref").isEmpty() && assistant) {
                  KisPaintingAssistantHandleSP handle = sideHandleMap.value(xml.attributes().value("ref").toString().toInt());
                  if (handle) {
                      assistant->addHandle(handle, HandleType::SIDE);
                  }
              }

            } else if (xml.name() == "assistant") {
                const KisPaintingAssistantFactory* factory = KisPaintingAssistantFactoryRegistry::instance()->get(xml.attributes().value("type").toString());

                if (factory) {
                    if (assistant) {
                        errors = true;
                        assistant.clear();
                    }
                    assistant = toQShared(factory->createPaintingAssistant());
                } else {
                    errors = true;
                }

                if (assistant) {
                    // load custom shared assistant properties
                    if (xml.attributes().hasAttribute("useCustomColor")) {
                        QStringRef useCustomColor = xml.attributes().value("useCustomColor");

                        bool usingColor = false;
                        if (useCustomColor.toString() == "1") {
                            usingColor = true;
                        }
                        assistant->setUseCustomColor(usingColor);
                    }

                    if ( xml.attributes().hasAttribute("useCustomColor")) {
                        QStringRef customColor = xml.attributes().value("customColor");
                        assistant->setAssistantCustomColor( KisDomUtils::qStringToQColor(customColor.toString()) );

                    }
                }
           }

            if (assistant) {
                assistant->loadCustomXml(&xml);
            }


           break;
        case QXmlStreamReader::EndElement:
            if (xml.name() == "assistant") {
                if (assistant) {
                    if (assistant->handles().size() == assistant->numHandles()) {
                        if (assistant->id() == "vanishing point" && sideHandleMap.empty()){
                        // Create side handles if the saved vp assistant doesn't have any.
                            QPointF pos = *assistant->handles()[0];
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(-70,0)), HandleType::SIDE);
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(-140,0)), HandleType::SIDE);
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(70,0)), HandleType::SIDE);
                            assistant->addHandle(new KisPaintingAssistantHandle(pos+QPointF(140,0)), HandleType::SIDE);
                        }
                        m_canvas->paintingAssistantsDecoration()->addAssistant(assistant);
                    } else {
                        errors = true;
                    }
                    assistant.clear();
                }
            }

            break;
        default:
            break;
        }

    }
    if (assistant) {
        errors = true;
        assistant.clear();
    }
    if (xml.hasError()) {
        QMessageBox::warning(qApp->activeWindow(), i18nc("@title:window", "Krita"), xml.errorString());
    }
    if (errors) {
        QMessageBox::warning(qApp->activeWindow(), i18nc("@title:window", "Krita"), i18n("Errors were encountered. Not all assistants were successfully loaded."));
    }

    KUndo2Command *command = new EditAssistantsCommand(m_canvas, m_origAssistantList, KisPaintingAssistant::cloneAssistantList(m_canvas->paintingAssistantsDecoration()->assistants()));
    m_canvas->viewManager()->undoAdapter()->addCommand(command);

    m_handles = m_canvas->paintingAssistantsDecoration()->handles();
    m_canvas->updateCanvas();
}

void KisAssistantTool::saveAssistants()
{

    if (m_handles.isEmpty()) return;

    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.writeStartDocument();
    xml.writeStartElement("paintingassistant");
    xml.writeAttribute("color",
                       KisDomUtils::qColorToQString(
                           m_canvas->paintingAssistantsDecoration()->globalAssistantsColor())); // global color if no custom color used


    xml.writeStartElement("handles");
    QMap<KisPaintingAssistantHandleSP, int> handleMap;
    Q_FOREACH (const KisPaintingAssistantHandleSP handle, m_handles) {
        int id = handleMap.size();
        handleMap.insert(handle, id);
        xml.writeStartElement("handle");
        //xml.writeAttribute("type", handle->handleType());
        xml.writeAttribute("id", QString::number(id));
        xml.writeAttribute("x", QString::number(double(handle->x()), 'f', 3));
        xml.writeAttribute("y", QString::number(double(handle->y()), 'f', 3));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeStartElement("sidehandles");
    QMap<KisPaintingAssistantHandleSP, int> sideHandleMap;
    Q_FOREACH (KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {
        Q_FOREACH (KisPaintingAssistantHandleSP handle, assistant->sideHandles()) {
            int id = sideHandleMap.size();
            sideHandleMap.insert(handle, id);
            xml.writeStartElement("sidehandle");
            xml.writeAttribute("id", QString::number(id));
            xml.writeAttribute("x", QString::number(double(handle->x()), 'f', 3));
            xml.writeAttribute("y", QString::number(double(handle->y()), 'f', 3));
            xml.writeEndElement();
        }
    }
    xml.writeStartElement("assistants");


    Q_FOREACH (const KisPaintingAssistantSP assistant, m_canvas->paintingAssistantsDecoration()->assistants()) {
        xml.writeStartElement("assistant");
        xml.writeAttribute("type", assistant->id());
        xml.writeAttribute("useCustomColor", QString::number(assistant->useCustomColor()));
        xml.writeAttribute("customColor",  KisDomUtils::qColorToQString(assistant->assistantCustomColor()));



        // custom assistant properties like angle density on vanishing point
        assistant->saveCustomXml(&xml);

        // handle information
        xml.writeStartElement("handles");
        Q_FOREACH (const KisPaintingAssistantHandleSP handle, assistant->handles()) {
            xml.writeStartElement("handle");
            xml.writeAttribute("ref", QString::number(handleMap.value(handle)));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        if (!sideHandleMap.empty()) {
            xml.writeStartElement("sidehandles");
            Q_FOREACH (const KisPaintingAssistantHandleSP handle, assistant->sideHandles()) {
                xml.writeStartElement("sidehandle");
                xml.writeAttribute("ref", QString::number(sideHandleMap.value(handle)));
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();

    KoFileDialog dialog(m_canvas->viewManager()->mainWindowAsQWidget(), KoFileDialog::SaveFile, "OpenAssistant");
    dialog.setCaption(i18n("Save Assistant"));
    dialog.setDefaultDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    dialog.setMimeTypeFilters(QStringList() << "application/x-krita-assistant", "application/x-krita-assistant");
    QString filename = dialog.filename();
    if (filename.isEmpty()) return;

    QFile file(filename);
    file.open(QIODevice::WriteOnly);
    file.write(data);
}

QWidget *KisAssistantTool::createOptionWidget()
{
    if (!m_optionsWidget) {
        m_optionsWidget = new QWidget;
        m_options.setupUi(m_optionsWidget);

        KConfigGroup cfg = KSharedConfig::openConfig()->group(toolId());

        // See https://bugs.kde.org/show_bug.cgi?id=316896
        QWidget *specialSpacer = new QWidget(m_optionsWidget);
        specialSpacer->setObjectName("SpecialSpacer");
        specialSpacer->setFixedSize(0, 0);
        m_optionsWidget->layout()->addWidget(specialSpacer);

        m_options.loadAssistantButton->setIcon(KisIconUtils::loadIcon("folder"));
        m_options.loadAssistantButton->setIconSize(QSize(16, 16));
        m_options.saveAssistantButton->setIcon(KisIconUtils::loadIcon("document-save-16"));
        m_options.saveAssistantButton->setIconSize(QSize(16, 16));
        m_options.deleteAllAssistantsButton->setIcon(KisIconUtils::loadIcon("edit-delete"));
        m_options.deleteAllAssistantsButton->setIconSize(QSize(16, 16));

        QList<KoID> assistants;
        Q_FOREACH (const QString& key, KisPaintingAssistantFactoryRegistry::instance()->keys()) {
            QString name = KisPaintingAssistantFactoryRegistry::instance()->get(key)->name();
            assistants << KoID(key, name);
        }
        std::sort(assistants.begin(), assistants.end(), KoID::compareNames);

        QString currentAssistantType = cfg.readEntry("AssistantType", "two point");
        int i = 0;
        int currentAssistantIndex = 0;
        Q_FOREACH(const KoID &id, assistants) {
            m_options.availableAssistantsComboBox->addItem(id.name(), id.id());
            if (id.id() == currentAssistantType) {
                currentAssistantIndex = i;
            }
            i++;
        }
        m_options.availableAssistantsComboBox->setCurrentIndex(currentAssistantIndex);

        connect(m_options.availableAssistantsComboBox, SIGNAL(currentIndexChanged(int)), SLOT(slotSelectedAssistantTypeChanged()));

        connect(m_options.saveAssistantButton, SIGNAL(clicked()), SLOT(saveAssistants()));
        connect(m_options.loadAssistantButton, SIGNAL(clicked()), SLOT(loadAssistants()));
        connect(m_options.deleteAllAssistantsButton, SIGNAL(clicked()), SLOT(removeAllAssistants()));

        connect(m_options.assistantsColor, SIGNAL(changed(QColor)), SLOT(slotGlobalAssistantsColorChanged(QColor)));
        connect(m_options.assistantsGlobalOpacitySlider, SIGNAL(valueChanged(int)), SLOT(slotGlobalAssistantOpacityChanged()));

        connect(m_options.vanishingPointAngleSpinbox, SIGNAL(valueChanged(double)), this, SLOT(slotChangeVanishingPointAngle(double)));
        connect(m_options.twoPointDensitySpinbox, SIGNAL(valueChanged(double)), this, SLOT(slotChangeTwoPointDensity(double)));
        connect(m_options.twoPointUseVerticalCheckbox, SIGNAL(stateChanged(int)), this, SLOT(slotChangeTwoPointUseVertical(int)));
        
        connect(m_options.subdivisionsSpinbox, SIGNAL(valueChanged(int)), this, SLOT(slotChangeSubdivisions(int)));
        connect(m_options.minorSubdivisionsSpinbox, SIGNAL(valueChanged(int)), this, SLOT(slotChangeMinorSubdivisions(int)));
        connect(m_options.fixedLengthCheckbox, SIGNAL(stateChanged(int)), this, SLOT(slotEnableFixedLength(int)));
        connect(m_options.fixedLengthSpinbox, SIGNAL(valueChangedPt(double)), this, SLOT(slotChangeFixedLength(double)));
        
        //update EditorWidgetData when checkbox clicked
        connect(m_options.showMove, SIGNAL(stateChanged(int)), this, SLOT(slotToggleMoveButton(int)));
        connect(m_options.showSnap, SIGNAL(stateChanged(int)), this, SLOT(slotToggleSnapButton(int)));
        connect(m_options.showLock, SIGNAL(stateChanged(int)), this, SLOT(slotToggleLockButton(int)));
        connect(m_options.showDuplicate, SIGNAL(stateChanged(int)), this, SLOT(slotToggleDuplicateButton(int)));
        connect(m_options.showDelete, SIGNAL(stateChanged(int)), this, SLOT(slotToggleDeleteButton(int)));
        
        // initialize UI elements with existing data if possible
        if (m_canvas && m_canvas->paintingAssistantsDecoration()) {
            const QColor color = m_canvas->paintingAssistantsDecoration()->globalAssistantsColor();

            QColor opaqueColor = color;
            opaqueColor.setAlpha(255);

            m_options.assistantsColor->setColor(opaqueColor);
            m_options.customAssistantColorButton->setColor(opaqueColor);
            m_options.assistantsGlobalOpacitySlider->setValue(color.alphaF() * 100.0);

        } else {
            m_options.assistantsColor->setColor(QColor(176, 176, 176, 255)); // grey default for all assistants
            m_options.assistantsGlobalOpacitySlider->setValue(100); // 100%
        }

        m_options.assistantsGlobalOpacitySlider->setPrefix(i18n("Opacity: "));
        m_options.assistantsGlobalOpacitySlider->setSuffix(" %");


        // custom color of selected assistant
        m_options.customColorOpacitySlider->setValue(100); // 100%
        m_options.customColorOpacitySlider->setPrefix(i18n("Opacity: "));
        m_options.customColorOpacitySlider->setSuffix(" %");

        connect(m_options.useCustomAssistantColor, SIGNAL(clicked(bool)), this, SLOT(slotUpdateCustomColor()));
        connect(m_options.customAssistantColorButton, SIGNAL(changed(QColor)), this, SLOT(slotUpdateCustomColor()));
        connect(m_options.customColorOpacitySlider, SIGNAL(valueChanged(int)), SLOT(slotCustomOpacityChanged()));

        m_options.twoPointDensitySpinbox->setPrefix(i18n("Density: "));
        m_options.twoPointDensitySpinbox->setRange(0.1, 4.0, 2);
        m_options.twoPointDensitySpinbox->setSingleStep(0.1);

        m_options.vanishingPointAngleSpinbox->setPrefix(i18n("Density: "));
        m_options.vanishingPointAngleSpinbox->setSuffix(QChar(Qt::Key_degree));
        m_options.vanishingPointAngleSpinbox->setRange(1.0, 180.0);
        m_options.vanishingPointAngleSpinbox->setSingleStep(1.0);
        
        m_options.subdivisionsSpinbox->setPrefix(i18n("Subdivisions: "));
        m_options.subdivisionsSpinbox->setRange(0, 100);
        m_options.subdivisionsSpinbox->setSoftRange(0, 20);
        
        m_options.minorSubdivisionsSpinbox->setPrefix(i18n("Minor Subdivisions: "));
        m_options.minorSubdivisionsSpinbox->setRange(1, 5);
        
        m_unitManager->setUnitDimension(KisSpinBoxUnitManager::LENGTH);
        m_options.fixedLengthSpinbox->setUnitManager(m_unitManager);
        m_options.fixedLengthSpinbox->setDisplayUnit(false);
        m_options.fixedLengthSpinbox->setMinimum(0);
        m_options.fixedLengthSpinbox->setDecimals(2);
        m_options.fixedLengthUnit->setModel(m_unitManager);
        m_unitManager->setApparentUnitFromSymbol("px");
        connect(m_options.fixedLengthUnit, SIGNAL(currentIndexChanged(int)), this, SLOT(slotChangeFixedLengthUnit(int)));
        connect(m_unitManager, SIGNAL(unitChanged(int)), m_options.fixedLengthUnit, SLOT(setCurrentIndex(int)));
        
        m_options.vanishingPointAngleSpinbox->setVisible(false);
        m_options.twoPointDensitySpinbox->setVisible(false);
        m_options.subdivisionsSpinbox->setVisible(false);
        m_options.minorSubdivisionsSpinbox->setVisible(false);
        m_options.fixedLengthCheckbox->setVisible(false);
        m_options.fixedLengthSpinbox->setVisible(false);
        m_options.fixedLengthUnit->setVisible(false);
        

        m_options.localAssistantCheckbox->setChecked(cfg.readEntry("LimitAssistantToArea", false));

        connect(m_options.localAssistantCheckbox, SIGNAL(stateChanged(int)), SLOT(slotLocalAssistantCheckboxChanged()));

        //set editor widget buttons on first startup.
        AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;


        if (globalEditorWidgetData.moveButtonActivated) {
            m_options.showMove->setChecked(true);
        }
        if (globalEditorWidgetData.snapButtonActivated) {
            m_options.showSnap->setChecked(true);
        }
        if (globalEditorWidgetData.lockButtonActivated) {
        m_options.showLock->setChecked(true);
        }
        if (globalEditorWidgetData.duplicateButtonActivated) {
            m_options.showDuplicate->setChecked(true);
        }
        if (globalEditorWidgetData.deleteButtonActivated) {
            m_options.showDelete->setChecked(true);
        }
    }

    updateToolOptionsUI();

    return m_optionsWidget;
}

void KisAssistantTool::slotGlobalAssistantsColorChanged(const QColor& setColor)
{
    // color and alpha are stored separately, so we need to merge the values before sending it on
    int oldAlpha = m_canvas->paintingAssistantsDecoration()->globalAssistantsColor().alpha();

    QColor newColor = setColor;
    newColor.setAlpha(oldAlpha);

    m_canvas->paintingAssistantsDecoration()->setGlobalAssistantsColor(newColor);

    m_canvas->paintingAssistantsDecoration()->uncache();
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotGlobalAssistantOpacityChanged()
{
    QColor newColor = m_canvas->paintingAssistantsDecoration()->globalAssistantsColor();
    qreal newOpacity = m_options.assistantsGlobalOpacitySlider->value() * 0.01 * 255.0;
    newColor.setAlpha(int(newOpacity));
    m_canvas->paintingAssistantsDecoration()->setGlobalAssistantsColor(newColor);

    m_canvas->paintingAssistantsDecoration()->uncache();
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotUpdateCustomColor()
{
    // get the selected assistant and change the angle value
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        m_selectedAssistant->setUseCustomColor(m_options.useCustomAssistantColor->isChecked());

        // changing color doesn't keep alpha, so update that before we send it on
        QColor newColor = m_options.customAssistantColorButton->color();
        newColor.setAlpha(m_selectedAssistant->assistantCustomColor().alpha());

        m_selectedAssistant->setAssistantCustomColor(newColor);
        m_selectedAssistant->uncache();
    }

    updateToolOptionsUI();
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotCustomOpacityChanged()
{
    KisPaintingAssistantSP m_selectedAssistant =  m_canvas->paintingAssistantsDecoration()->selectedAssistant();
    if (m_selectedAssistant) {
        QColor newColor = m_selectedAssistant->assistantCustomColor();
        qreal newOpacity = m_options.customColorOpacitySlider->value() * 0.01 * 255.0;
        newColor.setAlpha(int(newOpacity));
        m_selectedAssistant->setAssistantCustomColor(newColor);
        m_selectedAssistant->uncache();
    }

    // this forces the canvas to refresh to see the changes immediately
    m_canvas->paintingAssistantsDecoration()->uncache();
    m_canvas->updateCanvasDecorations();
}

void KisAssistantTool::slotLocalAssistantCheckboxChanged()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group(toolId());
    cfg.writeEntry("LimitAssistantToArea", m_options.localAssistantCheckbox->isChecked());
}

void KisAssistantTool::slotSelectedAssistantTypeChanged()
{
    updateToolOptionsUI();
}

void KisAssistantTool::slotToggleMoveButton(int index)
{
    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
    globalEditorWidgetData.moveButtonActivated  = (index > 0) ? true:false;
    updateEditorWidgetData();
}
void KisAssistantTool::slotToggleSnapButton(int index)
{
    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
    globalEditorWidgetData.snapButtonActivated  = (index > 0) ? true:false;
    updateEditorWidgetData();
}
void KisAssistantTool::slotToggleLockButton(int index)
{
    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
    globalEditorWidgetData.lockButtonActivated  = (index > 0) ? true:false;
    updateEditorWidgetData();
}
void KisAssistantTool::slotToggleDuplicateButton(int index)
{
    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
    globalEditorWidgetData.duplicateButtonActivated  = (index > 0) ? true:false;
    updateEditorWidgetData();
}
void KisAssistantTool::slotToggleDeleteButton(int index)
{
    AssistantEditorData &globalEditorWidgetData = m_canvas->paintingAssistantsDecoration()->globalEditorWidgetData;
    globalEditorWidgetData.deleteButtonActivated  = (index > 0) ? true:false;
    updateEditorWidgetData();
}

void KisAssistantTool::beginAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    Q_UNUSED(action);
    beginActionImpl(event);
}

void KisAssistantTool::continueAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    Q_UNUSED(action);
    continueActionImpl(event);
}

void KisAssistantTool::endAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    Q_UNUSED(action);
    endActionImpl(event);
}

void KisAssistantTool::beginPrimaryAction(KoPointerEvent *event)
{
    beginActionImpl(event);
}

void KisAssistantTool::continuePrimaryAction(KoPointerEvent *event)
{
    continueActionImpl(event);
}

void KisAssistantTool::endPrimaryAction(KoPointerEvent *event)
{
    endActionImpl(event);
}

bool KisAssistantTool::snap(KoPointerEvent *event)
{
    if (event->modifiers() == Qt::NoModifier) {
        return false;
    }

    if (m_handleDrag) {
        KisPaintingAssistantsDecorationSP canvasDecoration = m_canvas->paintingAssistantsDecoration();
        KisPaintingAssistantSP selectedAssistant = canvasDecoration->selectedAssistant();
        QList<KisPaintingAssistantHandleSP> handles = selectedAssistant->handles();

        if (selectedAssistant->id() == "two point" && m_handleDrag != handles[2] &&
            event->modifiers() != Qt::ShiftModifier) {
            // Snapping interactions that are specific to the two point assistant.
            // Skip this code block when only Shift is pressed, as
            // Shift means we only need closest-axis snapping.

            QSharedPointer<TwoPointAssistant> assis = qSharedPointerCast<TwoPointAssistant>(selectedAssistant);
            KisPaintingAssistantHandleSP handleOpp = m_handleDrag == handles[0] ? handles[1] : handles[0];
            const QPointF prevPoint = m_currentAdjustment.isNull() ? m_dragStart : m_currentAdjustment;

            qreal size = 0;
            const QTransform t = assis->localTransform(prevPoint,*handleOpp,*handles[2],&size);
            const QTransform inv = t.inverted();

            // Exact alignment matters here, so fudge horizon line
            // to be perfectly horizontal instead of trusting the
            // QTransform calculation to do it
            const QLineF horizon = QLineF(t.map(prevPoint), QPointF(t.map(*handleOpp).x(),t.map(prevPoint).y()));
            const QPointF sp = QPointF(0,horizon.p1().y()+size);

            const bool preserve_distortion_snap = event->modifiers() == Qt::ControlModifier;
            const bool preserve_left_right_ratio_snap = event->modifiers() == (Qt::ControlModifier|Qt::ShiftModifier);
            const bool preserve_horizon_snap = event->modifiers() == Qt::AltModifier;

            QPointF snap_point;
            QPointF opp_snap_point;
            QLineF sp_to_opp_vp;

            if (preserve_distortion_snap) {
                const QLineF sp_to_vp = QLineF(sp, t.map(*m_handleDrag));
                sp_to_opp_vp = sp_to_vp.normalVector();
                sp_to_vp.intersect(horizon,&snap_point);
            } else if (preserve_left_right_ratio_snap) {
                const QLineF prev_sp_to_vp = QLineF(sp, horizon.p1());
                QLineF new_sp_to_vp = prev_sp_to_vp.translated(t.map(*m_handleDrag)-sp);
                QPointF new_sp;
                new_sp_to_vp.intersect(QLineF(QPoint(0,0),QPointF(0,1)),&new_sp);
                sp_to_opp_vp = new_sp_to_vp.normalVector().translated(new_sp-new_sp_to_vp.p1());
                new_sp_to_vp.intersect(horizon,&snap_point);
            } else if (preserve_horizon_snap) {
                snap_point = QPointF(t.map(*m_handleDrag).x(),horizon.p1().y());
                sp_to_opp_vp = QLineF(sp,QPointF(t.map(prevPoint).x(),horizon.p1().y())).normalVector();
            }

            // The snapping modes must be robust against falling into
            // invalid configurations, so test if the new snap points
            // actually do make sense
            const bool no_intersection =
                // NB: opp_snap_point is initialized here
                sp_to_opp_vp.intersect(horizon, &opp_snap_point) == QLineF::NoIntersection;
            const bool origin_is_between =
                (snap_point.x() < 0 && opp_snap_point.x() > 0) ||
                (snap_point.x() > 0 && opp_snap_point.x() < 0);
            const bool null_opp_point =
                qFuzzyIsNull(opp_snap_point.x()) ||
                qFuzzyIsNull(opp_snap_point.y());
            const bool overlapping_snap_points =
                qFuzzyCompare(opp_snap_point.x(),snap_point.x());

            // Revert to original state if new points are invalid
            if (!origin_is_between || no_intersection || null_opp_point || overlapping_snap_points) {
                *m_handleDrag = m_dragStart;
                QPointF oppStart;
                // Use different recovery method for different
                // snapping modes
                if (preserve_distortion_snap) {
                    sp_to_opp_vp = QLineF(sp, t.map(m_dragStart)).normalVector();
                    sp_to_opp_vp.intersect(horizon, &oppStart);
                } else {
                    const QPointF p1 = t.map(m_dragStart);
                    const qreal p2x = preserve_horizon_snap ? t.map(*handleOpp).x() : -p1.x();
                    const QPointF p2 = QPointF(p2x,p1.y());
                    const QLineF new_horizon = QLineF(p1,p2);
                    const qreal new_size = sqrt(pow(new_horizon.length()/2.0,2) -
                                                pow(abs(new_horizon.center().x()),2));
                    const QPointF new_sp = QPointF(0,horizon.p1().y()+new_size);
                    sp_to_opp_vp = QLineF(new_sp, t.map(m_dragStart)).normalVector();
                }
                sp_to_opp_vp.intersect(horizon, &oppStart);
                *handleOpp=inv.map(oppStart);
                m_currentAdjustment = QPointF(0,0); // clear
            } else {
                // otherwise use the new configuration if it's valid
                *m_handleDrag = inv.map(snap_point);
                *handleOpp = inv.map(opp_snap_point);
                m_currentAdjustment = *m_handleDrag;
            }
        } else if (m_snapIsRadial == true) {
            QLineF dragRadius = QLineF(m_dragStart, event->point);
            dragRadius.setLength(m_radius.length());
            *m_handleDrag = dragRadius.p2();
        } else {
            QPointF snap_point = snapToClosestNiceAngle(event->point, m_dragStart);
            *m_handleDrag = snap_point;
        }
    } else {
        if (m_newAssistant && m_internalMode == MODE_CREATION) {
            QList<KisPaintingAssistantHandleSP> handles = m_newAssistant->handles();
            KisPaintingAssistantHandleSP handle_snap = handles.back();
            // for any assistant, snap 2nd handle to x or y axis relative to first handle
            if (handles.size() == 2) {
                QPointF snap_point = snapToClosestNiceAngle(event->point, (QPointF)(*handles[0]));
                *handle_snap =  snap_point;
            } else {
                bool was_snapped = false;
                if (m_newAssistant->id() == "spline") {
                    KisPaintingAssistantHandleSP start;
                    handles.size() == 3 ? start = handles[0] : start = handles[1];
                    QPointF snap_point = snapToClosestNiceAngle(event->point, (QPointF)(*start));
                    *handle_snap =  snap_point;
                    was_snapped = true;
                }

                if (m_newAssistant->id() == "ellipse" ||
                    m_newAssistant->id() == "concentric ellipse" ||
                    m_newAssistant->id() == "fisheye-point" ||
                    m_newAssistant->id() == "curvilinear-perspective") {
                    QPointF center = QLineF(*handles[0], *handles[1]).center();
                    QLineF radius = QLineF(center,*handles[0]);
                    QLineF dragRadius = QLineF(center, event->point);
                    dragRadius.setLength(radius.length());
                    *handle_snap = dragRadius.p2();
                    was_snapped = true;
                }

                if (m_newAssistant->id() == "perspective") {
                    KisPaintingAssistantHandleSP start;
                    handles.size() == 3 ? start = handles[1] : start = handles[2];
                    QPointF snap_point = snapToClosestNiceAngle(event->point, (QPointF)(*start));
                    *handle_snap = snap_point;
                    was_snapped = true;
                }
                return was_snapped;
            }
        }
    }
    return true;
}
