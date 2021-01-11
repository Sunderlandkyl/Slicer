/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/

#include "vtkSlicerROIWidget.h"

#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsROINode.h"
#include "vtkMRMLSliceNode.h"
#include "vtkSlicerROIRepresentation2D.h"
#include "vtkSlicerROIRepresentation3D.h"

// VTK includes
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkPointPlacer.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>

vtkStandardNewMacro(vtkSlicerROIWidget);

//----------------------------------------------------------------------
vtkSlicerROIWidget::vtkSlicerROIWidget()
{
  this->SetEventTranslationClickAndDrag(WidgetStateOnWidget, vtkCommand::LeftButtonPressEvent, vtkEvent::ShiftModifier,
    WidgetStateTranslatePlane, WidgetEventPlaneMoveStart, WidgetEventPlaneMoveEnd);
}

//----------------------------------------------------------------------
vtkSlicerROIWidget::~vtkSlicerROIWidget() = default;

//----------------------------------------------------------------------
void vtkSlicerROIWidget::CreateDefaultRepresentation(
  vtkMRMLMarkupsDisplayNode* markupsDisplayNode, vtkMRMLAbstractViewNode* viewNode, vtkRenderer* renderer)
{
  vtkSmartPointer<vtkSlicerMarkupsWidgetRepresentation> rep = nullptr;
  if (vtkMRMLSliceNode::SafeDownCast(viewNode))
    {
    rep = vtkSmartPointer<vtkSlicerROIRepresentation2D>::New();
    }
  else
    {
    rep = vtkSmartPointer<vtkSlicerROIRepresentation3D>::New();
    }
  this->SetRenderer(renderer);
  this->SetRepresentation(rep);
  rep->SetViewNode(viewNode);
  rep->SetMarkupsDisplayNode(markupsDisplayNode);
  rep->UpdateFromMRML(nullptr, 0); // full update
}

//----------------------------------------------------------------------
void vtkSlicerROIWidget::ScaleWidget(double eventPos[2])
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  vtkMRMLMarkupsROINode* markupsNode = vtkMRMLMarkupsROINode::SafeDownCast(this->GetMarkupsNode());
  if (!markupsNode || !displayNode)
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(markupsNode);

  double lastEventPos_World[3] = { 0.0 };
  double eventPos_World[3] = { 0.0 };
  double orientation_World[9] = { 0.0 };

  vtkSlicerMarkupsWidgetRepresentation* rep = vtkSlicerMarkupsWidgetRepresentation::SafeDownCast(this->WidgetRep);
  vtkSlicerROIRepresentation2D* rep2d = vtkSlicerROIRepresentation2D::SafeDownCast(this->WidgetRep);
  vtkSlicerROIRepresentation3D* rep3d = vtkSlicerROIRepresentation3D::SafeDownCast(this->WidgetRep);
  if (rep2d)
    {
    // 2D view
    double eventPos_Slice[3] = { 0. };
    eventPos_Slice[0] = this->LastEventPosition[0];
    eventPos_Slice[1] = this->LastEventPosition[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, lastEventPos_World);

    eventPos_Slice[0] = eventPos[0];
    eventPos_Slice[1] = eventPos[1];
    rep2d->GetSliceToWorldCoordinates(eventPos_Slice, eventPos_World);
    }
  else if (rep3d)
    {
    // 3D view
    double eventPos_Display[2] = { 0. };

    eventPos_Display[0] = this->LastEventPosition[0];
    eventPos_Display[1] = this->LastEventPosition[1];

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    lastEventPos_World[0] = eventPos_World[0];
    lastEventPos_World[1] = eventPos_World[1];
    lastEventPos_World[2] = eventPos_World[2];

    eventPos_Display[0] = eventPos[0];
    eventPos_Display[1] = eventPos[1];

    if (!rep3d->GetPointPlacer()->ComputeWorldPosition(this->Renderer,
      eventPos_Display, lastEventPos_World, eventPos_World,
      orientation_World))
      {
      return;
      }
    }

  if (this->GetActiveComponentType() == vtkMRMLMarkupsDisplayNode::ComponentScaleHandle)
    {
    vtkNew<vtkMatrix4x4> worldToROIMatrix;
    worldToROIMatrix->DeepCopy(markupsNode->GetROIToWorldMatrix());
    worldToROIMatrix->Invert();
    vtkNew<vtkTransform> worldToROITransform;
    worldToROITransform->SetMatrix(worldToROIMatrix);

    double lastEventPos_ROI[3] = { 0.0, 0.0, 0.0 };
    worldToROITransform->TransformPoint(lastEventPos_World, lastEventPos_ROI);

    double eventPos_ROI[3] = { 0.0, 0.0, 0.0 };
    worldToROITransform->TransformPoint(eventPos_World, eventPos_ROI);

    double scaleVector_ROI[3] = { 0.0, 0.0, 0.0 };
    int index = displayNode->GetActiveComponentIndex();

    double oldSideLengths[3] = { 0.0, 0.0, 0.0 };
    markupsNode->GetSideLengths(oldSideLengths);

    double newSideLengths[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Add(newSideLengths, eventPos_ROI, newSideLengths);
    vtkMath::MultiplyScalar(newSideLengths, 2.0);
    newSideLengths[0] = std::abs(newSideLengths[0]);
    newSideLengths[1] = std::abs(newSideLengths[1]);
    newSideLengths[2] = std::abs(newSideLengths[2]);

    switch (index)
      {
      case 0:
      case 1:
        newSideLengths[1] = oldSideLengths[1];
        newSideLengths[2] = oldSideLengths[2];
        break;
        break;
      case 2:
      case 3:
        newSideLengths[0] = oldSideLengths[0];
        newSideLengths[2] = oldSideLengths[2];
        break;
      case 4:
      case 5:
        newSideLengths[0] = oldSideLengths[0];
        newSideLengths[1] = oldSideLengths[1];
        break;
      default:
        break;
      }
    markupsNode->SetSideLengths(newSideLengths);

    double sideLengthDifference[3] = { 0.0, 0.0, 0.0 };
    vtkMath::Subtract(newSideLengths, oldSideLengths, sideLengthDifference);

    double origin_ROI[3] = { 0.0, 0.0, 0.0 };
    switch (index)
      {
      case 0:
        origin_ROI[0] = -0.5 * sideLengthDifference[0];
        break;
      case 1:
        origin_ROI[0] = 0.5 * sideLengthDifference[0];
        break;
      case 2:
        origin_ROI[1] = -0.5 * sideLengthDifference[1];
        break;
      case 3:
        origin_ROI[1] = 0.5 * sideLengthDifference[1];
        break;
      case 4:
        origin_ROI[2] = -0.5 * sideLengthDifference[2];
        break;
      case 5:
        origin_ROI[2] = 0.5 * sideLengthDifference[2];
        break;
      default:
        break;
      }

    double origin_World[3] = { 0.0, 0.0, 0.0 };
    vtkNew<vtkTransform> roiToWorldTransform;
    roiToWorldTransform->SetMatrix(markupsNode->GetROIToWorldMatrix());
    roiToWorldTransform->TransformPoint(origin_ROI, origin_World);
    markupsNode->SetOriginWorld(origin_World);
    }
}