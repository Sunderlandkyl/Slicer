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


#include "vtkMRMLTransformHandleWidget.h"
#include "vtkMRMLTransformHandleWidgetRepresentation.h"

#include "vtkMRMLApplicationLogic.h"
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLInteractionNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSliceCompositeNode.h"
#include "vtkMRMLSliceLogic.h"

// VTK includes
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkLine.h>
#include <vtkPlane.h>
#include <vtkPointPlacer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>
#include <vtkObjectFactory.h>

// MRML includes
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLSliceNode.h>
#include "vtkMRMLTransformNode.h"
#include <vtkMRMLLinearTransformNode.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLTransformHandleWidget);

//----------------------------------------------------------------------
vtkMRMLTransformHandleWidget::vtkMRMLTransformHandleWidget() = default;

//----------------------------------------------------------------------
vtkMRMLTransformHandleWidget::~vtkMRMLTransformHandleWidget() = default;

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidget::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
int vtkMRMLTransformHandleWidget::GetActiveComponentType()
{
  vtkSmartPointer<vtkMRMLTransformHandleWidgetRepresentation> rep = vtkMRMLTransformHandleWidgetRepresentation::SafeDownCast(this->WidgetRep);
  return rep->GetActiveComponentType();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidget::SetActiveComponentType(int type)
{
  vtkSmartPointer<vtkMRMLTransformHandleWidgetRepresentation> rep = vtkMRMLTransformHandleWidgetRepresentation::SafeDownCast(this->WidgetRep);
  rep->SetActiveComponentType(type);
}

//----------------------------------------------------------------------
int vtkMRMLTransformHandleWidget::GetActiveComponentIndex()
{
  vtkSmartPointer<vtkMRMLTransformHandleWidgetRepresentation> rep = vtkMRMLTransformHandleWidgetRepresentation::SafeDownCast(this->WidgetRep);
  return rep->GetActiveComponentIndex();
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidget::SetActiveComponentIndex(int index)
{
  vtkSmartPointer<vtkMRMLTransformHandleWidgetRepresentation> rep = vtkMRMLTransformHandleWidgetRepresentation::SafeDownCast(this->WidgetRep);
  rep->SetActiveComponentIndex(index);
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidget::ApplyTransform(vtkTransform* transform)
{
  if (!this->GetTransformNode())
    {
    return;
    }

  MRMLNodeModifyBlocker transformBlocker(this->GetTransformNode());

  vtkNew<vtkMatrix4x4> transformFromParent;
  this->GetTransformNode()->GetMatrixTransformToParent(transformFromParent);
  vtkNew<vtkTransform> t;
  t->Concatenate(transform);
  t->Concatenate(transformFromParent);
  this->GetTransformNode()->SetMatrixTransformToParent(t->GetMatrix());
}

//----------------------------------------------------------------------
void vtkMRMLTransformHandleWidget::CreateDefaultRepresentation(vtkMRMLTransformDisplayNode* displayNode,
  vtkMRMLAbstractViewNode* viewNode, vtkRenderer* renderer)
{
  vtkSmartPointer<vtkMRMLTransformHandleWidgetRepresentation> rep = vtkSmartPointer<vtkMRMLTransformHandleWidgetRepresentation>::New();
  this->SetRenderer(renderer);
  this->SetRepresentation(rep);
  rep->SetViewNode(viewNode);
  rep->SetDisplayNode(displayNode);
  rep->UpdateFromMRML(nullptr, 0); // full update
}

//----------------------------------------------------------------------
vtkMRMLTransformDisplayNode* vtkMRMLTransformHandleWidget::GetDisplayNode()
{
  vtkMRMLTransformHandleWidgetRepresentation* widgetRep = vtkMRMLTransformHandleWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!widgetRep)
    {
    return nullptr;
    }
  return widgetRep->GetDisplayNode();
}

//----------------------------------------------------------------------
vtkMRMLTransformNode* vtkMRMLTransformHandleWidget::GetTransformNode()
{
  vtkMRMLTransformHandleWidgetRepresentation* widgetRep = vtkMRMLTransformHandleWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!widgetRep)
  {
    return nullptr;
  }
  return widgetRep->GetTransformNode();
}
