/*==============================================================================

  Program: 3D Slicer

  Copyright (c) Brigham and Women's Hospital

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#include "vtkMRMLLabelsWidget.h"
#include "vtkMRMLLabelsWidgetRepresentation.h"

// MRML includes
#include <vtkMRMLLabelDisplayNode.h>

// VTK includes
#include <vtkObjectFactory.h>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLLabelsWidget);

//----------------------------------------------------------------------------
vtkMRMLLabelsWidget::vtkMRMLLabelsWidget()
{
  this->LabelDisplayNode = nullptr;
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidget::~vtkMRMLLabelsWidget()
{
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidget::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidget::SetLabelDisplayNode(vtkMRMLLabelDisplayNode* displayNode)
{
  if (this->LabelDisplayNode == displayNode)
  {
    return;
  }

  this->LabelDisplayNode = displayNode;

  vtkMRMLLabelsWidgetRepresentation* rep = this->GetLabelsRepresentation();
  if (rep)
  {
    rep->SetLabelDisplayNode(displayNode);
  }

  this->Modified();
}

//----------------------------------------------------------------------------
vtkMRMLLabelDisplayNode* vtkMRMLLabelsWidget::GetLabelDisplayNode()
{
  return this->LabelDisplayNode;
}

//----------------------------------------------------------------------------
vtkMRMLLabelsWidgetRepresentation* vtkMRMLLabelsWidget::GetLabelsRepresentation()
{
  return vtkMRMLLabelsWidgetRepresentation::SafeDownCast(this->WidgetRep);
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidget::CreateDefaultRepresentation()
{
  // Subclasses should override this to create the appropriate representation type
}

//----------------------------------------------------------------------------
void vtkMRMLLabelsWidget::UpdateFromRenderer()
{
  vtkMRMLLabelsWidgetRepresentation* rep = this->GetLabelsRepresentation();
  if (rep)
  {
    rep->UpdateLabelPositions();
    rep->NeedToRenderOn();
  }
}
